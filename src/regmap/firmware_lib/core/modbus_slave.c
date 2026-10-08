/**
 * @file       modbus_slave.c
 * @brief      Modbus RTU slave
 * @regmap-lib
 * @addtogroup grMbSlave
 * @{
 */

/* Includes ------------------------------------------------------------------*/

#include "modbus_slave.h"

#if REGMAP_LIB_MODBUS

#include "lib_bytes.h"
#include "mb_rtu_app.h"
#include "port.h"
#if REGMAP_LIB_UPGRADE
#include "mb_upgrade.h"
#endif

/* Private defines -----------------------------------------------------------*/

#define MB_BROADCAST            0
#define MB_MIN_SLAVE_ADDR       1
#define MB_MAX_SLAVE_ADDR       247
#define MB_EXCEPTION_MASK       0x80
#define MB_FC_READ_HOLDING      3
#define MB_FC_READ_INPUT        4
#define MB_FC_WRITE_SINGLE      6
#define MB_FC_WRITE_MULTIPLE    16
#define MB_EX_ILLEGAL_FUNCTION  1
#define MB_EX_ILLEGAL_ADDRESS   2
#define MB_EX_ILLEGAL_VALUE     3
#define MB_MAX_READ_COUNT       125
#define MB_MAX_WRITE_COUNT      123
#define MB_MIN_GAP_MS           2u
#define MB_DEFAULT_BAUD_RATE    19200u

/* Private typedefs ----------------------------------------------------------*/

typedef struct
{
  MbSlave_Config_t cfg;           ///< Active settings
  MbSlave_Config_t pending;       ///< Settings waiting for the end of the response
  bool reconfigure;               ///< pending is valid
  uint8_t rx[MBSLAVE_FRAME_MAX];  ///< Received chunks of the current frame
  volatile uint16_t rxLen;        ///< Bytes in rx (ISR appends, Handle resets)
  volatile uint32_t rxTick;       ///< Tick of the last received chunk
  volatile bool txBusy;           ///< Response handed to the port, not finished yet
  uint8_t tx[MBSLAVE_FRAME_MAX];  ///< Response frame
  uint32_t gapMs;                 ///< t3.5 rounded up, at least MB_MIN_GAP_MS
  uint32_t lastFrameTick;         ///< Tick of the last valid frame for this slave
  bool timeout;                   ///< Communication timeout active
  volatile uint32_t errors;       ///< Dropped frames and chunks
} MbSlave_Private_t;

/* Private variables ---------------------------------------------------------*/

static MbSlave_Private_t mb;

/* Private function prototypes -----------------------------------------------*/

static void MbSlave_Apply(const MbSlave_Config_t *cfg);
static void MbSlave_ClearRx(void);
static uint16_t MbSlave_Process(const uint8_t *req, uint16_t len, uint8_t *resp);
static uint16_t MbSlave_Exception(uint8_t *resp, uint8_t fc, uint8_t code);
static bool MbSlave_InRange(uint16_t addr, uint16_t count, int32_t last);

/* Functions -----------------------------------------------------------------*/

Status_t MbSlave_Init(const MbSlave_Config_t *cfg)
{
  memset(&mb, 0, sizeof(mb));
  MbSlave_Apply(cfg);
  mb.lastFrameTick = Port_GetTickMs();
#if REGMAP_LIB_UPGRADE
  MbUpgr_Init();
#endif

  return STATUS_OK;
}


Status_t MbSlave_Reconfigure(const MbSlave_Config_t *cfg)
{
  mb.pending = *cfg;
  mb.reconfigure = true;

  return STATUS_OK;
}


Status_t MbSlave_Handle(void)
{
  uint32_t now = Port_GetTickMs();
  uint32_t rxTick;
  uint16_t len;
  uint16_t respLen;

  Port_CriticalEnter();
  len = mb.rxLen;
  rxTick = mb.rxTick;
  Port_CriticalExit();

  if (len > 0 && !mb.txBusy)
  {
    if (len >= 4 && MbSlave_Crc16(mb.rx, (uint16_t)(len - 2)) == Bytes_GetU16Le(mb.rx + len - 2))
    {
      if (mb.rx[0] == mb.cfg.address || mb.rx[0] == MB_BROADCAST)
      {
        respLen = MbSlave_Process(mb.rx, (uint16_t)(len - 2), mb.tx);
        mb.lastFrameTick = now;

        if (mb.rx[0] != MB_BROADCAST)
        {
          Bytes_PutU16Le(mb.tx + respLen, MbSlave_Crc16(mb.tx, respLen));
          mb.txBusy = true;
          if (MbPort_Send(mb.tx, (uint16_t)(respLen + 2)) != STATUS_OK)
          {
            mb.txBusy = false;
            mb.errors++;
          }
        }
      }
      MbSlave_ClearRx();
    }
    else if (now - rxTick >= mb.gapMs)
    {
      /* Incomplete or corrupted frame */
      MbSlave_ClearRx();
      mb.errors++;
    }
  }

  if (mb.reconfigure && !mb.txBusy)
  {
    mb.reconfigure = false;
    MbSlave_Apply(&mb.pending);
  }

  if (!mb.timeout && mb.cfg.timeoutMs != 0 && now - mb.lastFrameTick >= mb.cfg.timeoutMs)
  {
    mb.timeout = true;
    MbSlave_TimeoutChanged(true);
  }
  else if (mb.timeout && (mb.cfg.timeoutMs == 0 || now - mb.lastFrameTick < mb.cfg.timeoutMs))
  {
    mb.timeout = false;
    MbSlave_TimeoutChanged(false);
  }

#if REGMAP_LIB_UPGRADE
  if (!mb.txBusy)
  {
    (void)MbUpgr_Handle();
  }
#endif

  return STATUS_OK;
}


bool MbSlave_IsTimeout(void)
{
  return mb.timeout;
}


uint32_t MbSlave_GetErrorCount(void)
{
  return mb.errors;
}


uint16_t MbSlave_Crc16(const uint8_t *data, uint16_t len)
{
  uint16_t crc = 0xFFFFu;
  uint16_t i;
  uint8_t bit;

  for (i = 0; i < len; i++)
  {
    crc ^= data[i];
    for (bit = 0; bit < 8; bit++)
    {
      crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xA001u) : (uint16_t)(crc >> 1);
    }
  }

  return crc;
}


void MbSlave_RxFrame(const uint8_t *data, uint16_t len)
{
  uint16_t rxLen = mb.rxLen;

  if (mb.txBusy)
  {
    return;
  }
  if (len > MBSLAVE_FRAME_MAX - rxLen)
  {
    mb.rxLen = 0;
    mb.errors++;
    return;
  }

  memcpy(mb.rx + rxLen, data, len);
  mb.rxTick = Port_GetTickMs();
  mb.rxLen = (uint16_t)(rxLen + len);
  MbPort_NotifyFromIsr();
}


void MbSlave_TxDone(void)
{
  mb.txBusy = false;
}

/* Private Functions ---------------------------------------------------------*/

static void MbSlave_Apply(const MbSlave_Config_t *cfg)
{
  mb.cfg = *cfg;
  SAT_DOWN(mb.cfg.address, MB_MIN_SLAVE_ADDR);
  SAT_UP(mb.cfg.address, MB_MAX_SLAVE_ADDR);
  if (mb.cfg.baudRate == 0)
  {
    mb.cfg.baudRate = MB_DEFAULT_BAUD_RATE;
  }

  /* t3.5 = 3.5 characters of 11 bits, rounded up, +1 ms for the tick resolution */
  mb.gapMs = (38500u + mb.cfg.baudRate - 1u) / mb.cfg.baudRate + 1u;
  SAT_DOWN(mb.gapMs, MB_MIN_GAP_MS);

  MbSlave_ClearRx();
  (void)MbPort_Init(&mb.cfg);
}


static void MbSlave_ClearRx(void)
{
  Port_CriticalEnter();
  mb.rxLen = 0;
  Port_CriticalExit();
}


static uint16_t MbSlave_Exception(uint8_t *resp, uint8_t fc, uint8_t code)
{
  resp[1] = (uint8_t)(fc | MB_EXCEPTION_MASK);
  resp[2] = code;

  return 3;
}


static bool MbSlave_InRange(uint16_t addr, uint16_t count, int32_t last)
{
  return (int32_t)addr + (int32_t)count - 1 <= last;
}

#if REGMAP_LIB_UPGRADE
static bool MbSlave_InUpgrade(uint16_t addr, uint16_t count)
{
  return addr >= MB_UPGR_BASE_ADDRESS && (uint32_t)addr + count - 1u <= MB_UPGR_END_ADDRESS;
}
#endif

/**
 * Process a request (without CRC) and build the response (without CRC).
 * @return Length of the response
 */
static uint16_t MbSlave_Process(const uint8_t *req, uint16_t len, uint8_t *resp)
{
  uint8_t fc = req[1];
  uint16_t addr;
  uint16_t count;
  uint16_t value;
  uint16_t i;

  resp[0] = mb.cfg.address;
  resp[1] = fc;

  if (fc != MB_FC_READ_HOLDING && fc != MB_FC_READ_INPUT && fc != MB_FC_WRITE_SINGLE
      && fc != MB_FC_WRITE_MULTIPLE)
  {
    return MbSlave_Exception(resp, fc, MB_EX_ILLEGAL_FUNCTION);
  }
  if (len < 6)
  {
    return MbSlave_Exception(resp, fc, MB_EX_ILLEGAL_VALUE);
  }

  addr = Bytes_GetU16Be(req + 2);
  count = Bytes_GetU16Be(req + 4); /* FC 6: the value */

  if (fc == MB_FC_READ_HOLDING || fc == MB_FC_READ_INPUT)
  {
    if (len != 6 || count < 1 || count > MB_MAX_READ_COUNT)
    {
      return MbSlave_Exception(resp, fc, MB_EX_ILLEGAL_VALUE);
    }
#if REGMAP_LIB_UPGRADE
    if (fc == MB_FC_READ_HOLDING && MbSlave_InUpgrade(addr, count))
    {
      (void)MbUpgr_ReadRegisters(addr, count, resp + 3);
    }
    else
#endif
    {
      if (!MbSlave_InRange(addr, count, fc == MB_FC_READ_HOLDING ? MB_HOLD_LAST : MB_INPUT_LAST))
      {
        return MbSlave_Exception(resp, fc, MB_EX_ILLEGAL_ADDRESS);
      }
      for (i = 0; i < count; i++)
      {
        if (fc == MB_FC_READ_HOLDING)
        {
          (void)MbRtu_ReadHoldingRegCallback((uint16_t)(addr + i), &value);
        }
        else
        {
          (void)MbRtu_ReadInputRegCallback((uint16_t)(addr + i), &value);
        }
        /* The callbacks return the value already byte-swapped for the wire */
        memcpy(resp + 3 + 2 * i, &value, 2);
      }
    }
    resp[2] = (uint8_t)(count * 2);
    return (uint16_t)(3 + count * 2);
  }

  if (fc == MB_FC_WRITE_SINGLE)
  {
    if (len != 6)
    {
      return MbSlave_Exception(resp, fc, MB_EX_ILLEGAL_VALUE);
    }
#if REGMAP_LIB_UPGRADE
    if (MbSlave_InUpgrade(addr, 1))
    {
      (void)MbUpgr_WriteRegisters(addr, 1, req + 4);
    }
    else
#endif
    {
      if (!MbSlave_InRange(addr, 1, MB_HOLD_LAST))
      {
        return MbSlave_Exception(resp, fc, MB_EX_ILLEGAL_ADDRESS);
      }
      (void)MbRtu_WriteHoldingRegCallback(addr, count);
    }
    memcpy(resp + 2, req + 2, 4);
    return 6;
  }

  /* MB_FC_WRITE_MULTIPLE */
  if (count < 1 || count > MB_MAX_WRITE_COUNT || len < 7 || req[6] != count * 2
      || len != 7 + count * 2)
  {
    return MbSlave_Exception(resp, fc, MB_EX_ILLEGAL_VALUE);
  }
#if REGMAP_LIB_UPGRADE
  if (MbSlave_InUpgrade(addr, count))
  {
    (void)MbUpgr_WriteRegisters(addr, count, req + 7);
  }
  else
#endif
  {
    if (!MbSlave_InRange(addr, count, MB_HOLD_LAST))
    {
      return MbSlave_Exception(resp, fc, MB_EX_ILLEGAL_ADDRESS);
    }
    for (i = 0; i < count; i++)
    {
      (void)MbRtu_WriteHoldingRegCallback((uint16_t)(addr + i), Bytes_GetU16Be(req + 7 + 2 * i));
    }
  }
  memcpy(resp + 2, req + 2, 4);
  return 6;
}

#endif /* REGMAP_LIB_MODBUS */

/** @} */
