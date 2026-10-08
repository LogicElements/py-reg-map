/**
 * @file       lebin.c
 * @brief      LeBin binary protocol
 * @regmap-lib
 * @addtogroup grLebin
 * @{
 */

/* Includes ------------------------------------------------------------------*/

#include "lebin.h"

#if REGMAP_LIB_LEBIN

#include "configuration.h"
#include "lib_bytes.h"
#include "port.h"
#if REGMAP_LIB_UPGRADE
#include "fw_upgrade.h"
#endif

/* FW upgrade return codes in the acknowledge packet */
#define LEBIN_UPG_OK            0u
#define LEBIN_UPG_ERROR         1u
#define LEBIN_UPG_WRONG_LENGTH  2u
#define LEBIN_UPG_ALIGN         32u   ///< Data length must be a multiple of this

#if (LEBIN_RX_BUFFER_SIZE & (LEBIN_RX_BUFFER_SIZE - 1u)) != 0
#error "LEBIN_RX_BUFFER_SIZE must be a power of two"
#endif

/* Private typedefs ----------------------------------------------------------*/

typedef struct
{
  uint8_t ring[LEBIN_RX_BUFFER_SIZE];   ///< Received bytes (written in ISR)
  volatile uint32_t head;               ///< Written by Lebin_RxBytes only
  volatile uint32_t tail;               ///< Written by Lebin_Handle only
  volatile uint32_t rxTick;             ///< Tick of the last received chunk
  volatile uint32_t errors;             ///< Dropped chunks and bytes
  uint8_t packet[LEBIN_PACKET_MAX];     ///< Packet being assembled
  uint32_t packetLen;                   ///< Bytes in packet
  uint8_t tx[LEBIN_PACKET_MAX];         ///< Response, owned by the port until sent
} Lebin_Private_t;

/* Private variables ---------------------------------------------------------*/

static Lebin_Private_t lb;

/* Private function prototypes -----------------------------------------------*/

static void Lebin_Drop(uint32_t count);
static uint32_t Lebin_ReadReg(const uint8_t *req, uint32_t length, uint8_t *resp);
static uint32_t Lebin_WriteReg(const uint8_t *req, uint32_t length, uint8_t *resp);
#if REGMAP_LIB_UPGRADE
static uint32_t Lebin_FwUpgrade(const uint8_t *req, uint32_t length, uint8_t *resp);
#endif

/* Functions -----------------------------------------------------------------*/

Status_t Lebin_Init(void)
{
  memset(&lb, 0, sizeof(lb));

  return LebinPort_Init();
}


void Lebin_RxBytes(const uint8_t *data, uint32_t len)
{
  uint32_t head = lb.head;
  uint32_t i;

  if (len > LEBIN_RX_BUFFER_SIZE - (head - lb.tail))
  {
    lb.errors++;
    return;
  }

  for (i = 0; i < len; i++)
  {
    lb.ring[(head + i) & (LEBIN_RX_BUFFER_SIZE - 1u)] = data[i];
  }
  lb.rxTick = Port_GetTickMs();
  lb.head = head + len;
  LebinPort_NotifyFromIsr();
}


Status_t Lebin_Handle(void)
{
  uint32_t head = lb.head;
  uint32_t length;
  uint16_t respLen;
  const uint8_t *start;

  /* Move received bytes into the packet buffer */
  while (lb.tail != head && lb.packetLen < LEBIN_PACKET_MAX)
  {
    lb.packet[lb.packetLen++] = lb.ring[lb.tail & (LEBIN_RX_BUFFER_SIZE - 1u)];
    lb.tail++;
  }

  while (lb.packetLen > 0)
  {
    if (lb.packet[0] != LEBIN_START_BYTE)
    {
      /* Resynchronise on the next start byte */
      start = memchr(lb.packet + 1, LEBIN_START_BYTE, lb.packetLen - 1);
      Lebin_Drop((start != NULL) ? (uint32_t)(start - lb.packet) : lb.packetLen);
      lb.errors++;
      continue;
    }
    if (lb.packetLen < LEBIN_HEADER_LEN)
    {
      break;
    }
    length = Bytes_GetU16Le(lb.packet + 2);
    if (length < LEBIN_MIN_LENGTH || length > LEBIN_PACKET_MAX)
    {
      /* Not a packet start */
      Lebin_Drop(1);
      lb.errors++;
      continue;
    }
    if (lb.packetLen < length)
    {
      break;
    }
    if (!LebinPort_TxReady())
    {
      return STATUS_BUSY;
    }

    respLen = Lebin_ProcessPacket(lb.packet, lb.tx);
    Lebin_Drop(length);
    if (respLen != 0)
    {
      if (LebinPort_Send(lb.tx, respLen) != STATUS_OK)
      {
        lb.errors++;
      }
      return STATUS_OK;
    }
  }

  /* A partial packet without new bytes for too long is abandoned */
  if (lb.packetLen > 0 && Port_GetTickMs() - lb.rxTick > LEBIN_NEW_PACKET_MS)
  {
    lb.packetLen = 0;
    lb.errors++;
  }

  return STATUS_OK;
}


uint16_t Lebin_ProcessPacket(const uint8_t *req, uint8_t *resp)
{
  uint32_t length = Bytes_GetU16Le(req + 2);
  uint32_t respLen = 0;

  switch (req[1])
  {
    case LEBIN_READ_REG:
      respLen = Lebin_ReadReg(req, length, resp);
      break;

    case LEBIN_WRITE_REG:
      respLen = Lebin_WriteReg(req, length, resp);
      break;

#if REGMAP_LIB_UPGRADE
    case LEBIN_FW_UPGRADE:
      respLen = Lebin_FwUpgrade(req, length, resp);
      break;
#endif

    default:
      /* Unknown packet: no response */
      break;
  }

  if (respLen != 0)
  {
    resp[0] = LEBIN_START_BYTE;
    Bytes_PutU16Le(resp + 2, (uint16_t)respLen);
  }

  return (uint16_t)respLen;
}


Status_t Lebin_FillSeries(uint8_t *buffer, uint32_t id, uint32_t timestamp, uint32_t delta,
                          uint32_t count)
{
  uint32_t length = LEBIN_HEADER_LEN + 16 + count * CONF_BYTE_LEN_ID(id);

  buffer[0] = LEBIN_START_BYTE;
  buffer[1] = LEBIN_TIME_SER;
  Bytes_PutU16Le(buffer + 2, (uint16_t)length);
  Bytes_PutU32Le(buffer + 4, id);
  Bytes_PutU32Le(buffer + 8, timestamp);
  Bytes_PutU32Le(buffer + 12, delta);
  Bytes_PutU32Le(buffer + 16, count);

  return (length > 0xFFFFu) ? STATUS_ERROR : STATUS_OK;
}


uint32_t Lebin_GetErrorCount(void)
{
  return lb.errors;
}

/* Private Functions ---------------------------------------------------------*/

static void Lebin_Drop(uint32_t count)
{
  memmove(lb.packet, lb.packet + count, lb.packetLen - count);
  lb.packetLen -= count;
}


/** Response: ID + value for every known ID; unknown IDs are left out */
static uint32_t Lebin_ReadReg(const uint8_t *req, uint32_t length, uint8_t *resp)
{
  uint32_t reqIdx = LEBIN_HEADER_LEN;
  uint32_t respIdx = LEBIN_HEADER_LEN;
  uint32_t id;
  uint32_t size;

  resp[1] = LEBIN_READ_REG_RESP;

  while (reqIdx + 4 <= length)
  {
    id = Bytes_GetU32Le(req + reqIdx);
    reqIdx += 4;
    if (Config_CheckLimits(id) != STATUS_OK)
    {
      continue;
    }
    size = CONF_BYTE_LEN_ID(id);
    if (respIdx + 4 + size > LEBIN_PACKET_MAX)
    {
      break;
    }
    Bytes_PutU32Le(resp + respIdx, id);
    memcpy(resp + respIdx + 4, CONF_PTR(id), size);
    respIdx += 4 + size;
  }

  return respIdx;
}


/** Response: ID + stored value for every pair; 0xF0 bytes for unknown IDs */
static uint32_t Lebin_WriteReg(const uint8_t *req, uint32_t length, uint8_t *resp)
{
  uint32_t idx = LEBIN_HEADER_LEN;
  uint32_t id;
  uint32_t size;

  resp[1] = LEBIN_READ_REG_RESP;

  while (idx + 4 <= length)
  {
    id = Bytes_GetU32Le(req + idx);
    size = CONF_BYTE_LEN_ID(id);
    if (idx + 4 + size > length)
    {
      break;
    }
    Bytes_PutU32Le(resp + idx, id);
    if (Config_CheckLimits(id) == STATUS_OK)
    {
      memcpy(CONF_PTR(id), req + idx + 4, size);
      (void)Config_ApplyConfig(id);
      memcpy(resp + idx + 4, CONF_PTR(id), size);
    }
    else
    {
      memset(resp + idx + 4, 0xF0, size);
    }
    idx += 4 + size;
  }

  return idx;
}

#if REGMAP_LIB_UPGRADE
/**
 * Request: offset (4 bytes) + data; data length 0 ends the image.
 * Acknowledge: offset + data length, return code.
 */
static uint32_t Lebin_FwUpgrade(const uint8_t *req, uint32_t length, uint8_t *resp)
{
  uint32_t offset = Bytes_GetU32Le(req + 4);
  uint32_t dataLength = length - LEBIN_MIN_LENGTH;
  uint32_t retCode;

  if (dataLength == 0)
  {
    retCode = (Upgrade_Finish() == STATUS_OK) ? LEBIN_UPG_OK : LEBIN_UPG_ERROR;
  }
  else if (dataLength % LEBIN_UPG_ALIGN != 0)
  {
    retCode = LEBIN_UPG_WRONG_LENGTH;
  }
  else
  {
    if (offset == 0)
    {
      (void)Upgrade_Begin(0);
    }
    retCode = (Upgrade_Write(offset, req + LEBIN_MIN_LENGTH, dataLength) == STATUS_OK)
              ? LEBIN_UPG_OK : LEBIN_UPG_ERROR;
  }

  resp[1] = LEBIN_FW_UPG_ACK;
  Bytes_PutU32Le(resp + 4, offset + dataLength);
  Bytes_PutU32Le(resp + 8, retCode);

  return 12;
}
#endif

#endif /* REGMAP_LIB_LEBIN */

/** @} */
