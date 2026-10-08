/**
 * @file       mb_upgrade.c
 * @brief      Firmware upgrade over Modbus RTU holding registers
 * @regmap-lib
 * @addtogroup grMbUpgrade
 * @{
 */

#include "mb_upgrade.h"

#if REGMAP_LIB_UPGRADE

#include "fw_upgrade.h"
#include <stddef.h>

/** Register storage: register n = bytes 2n..2n+1 (little-endian words) */
typedef struct __packed
{
  uint16_t type;                     ///< Type of binary (application specific)
  uint16_t mode;                     ///< MbUpgr_Mode_t
  uint32_t size;                     ///< Size of binary in bytes
  uint16_t page_size;                ///< Size of current page in bytes
  uint32_t offset;                   ///< Offset of current page
  uint8_t data[MB_UPGR_PAGE_SIZE];   ///< Current page data
  uint16_t status;                   ///< MbUpgr_Status_t
  uint16_t writeDone;                ///< 1 = page complete, program it
  uint16_t reserved;                 ///< Last register of the range
} MbUpgr_Registers_t;

/* The storage must cover the whole register range */
typedef char MbUpgr_SizeCheck_t[(sizeof(MbUpgr_Registers_t)
                                 == (MB_UPGR_END_ADDRESS - MB_UPGR_BASE_ADDRESS + 1) * 2) ? 1 : -1];

/** Register index of writeDone */
#define MB_UPGR_WRITE_DONE_REG  (offsetof(MbUpgr_Registers_t, writeDone) / 2)

typedef struct
{
  MbUpgr_Registers_t regs;
  bool headerWritten;  ///< One of the first 4 registers written
} MbUpgr_Private_t;

static MbUpgr_Private_t mbu;

static bool MbUpgr_InRange(uint16_t address, uint16_t count)
{
  return address >= MB_UPGR_BASE_ADDRESS && count > 0
         && (uint32_t)address + count - 1u <= MB_UPGR_END_ADDRESS;
}


Status_t MbUpgr_Init(void)
{
  memset(&mbu, 0, sizeof(mbu));
  mbu.regs.status = MB_UPGR_STATUS_BUSY;

  return STATUS_OK;
}


Status_t MbUpgr_Handle(void)
{
  Status_t ret = STATUS_OK;

  if (mbu.headerWritten)
  {
    mbu.headerWritten = false;
    /* The session itself starts with the page at offset 0, so a master may rewrite the
     * header before every page */
    if (mbu.regs.mode == MB_UPGR_MODE_APPLY)
    {
      Upgrade_Apply();
    }
    if (mbu.regs.writeDone == 0)
    {
      mbu.regs.status = MB_UPGR_STATUS_READY;
    }
  }

  if (mbu.regs.writeDone != 0)
  {
    if (mbu.regs.page_size > MB_UPGR_PAGE_SIZE)
    {
      ret = STATUS_ERROR;
    }
    else
    {
      if (mbu.regs.offset == 0)
      {
        ret = Upgrade_Begin((mbu.regs.mode == MB_UPGR_MODE_ERASE_AT_START) ? mbu.regs.size : 0);
      }
      if (ret == STATUS_OK)
      {
        ret = Upgrade_Write(mbu.regs.offset, mbu.regs.data, mbu.regs.page_size);
      }
    }
    mbu.regs.status = (ret == STATUS_OK) ? MB_UPGR_STATUS_READY : MB_UPGR_STATUS_DONE_ERROR;

    /* Last page received? */
    if (ret == STATUS_OK && mbu.regs.size - mbu.regs.offset <= mbu.regs.page_size)
    {
      ret = Upgrade_Finish();
      mbu.regs.status = (ret == STATUS_OK) ? MB_UPGR_STATUS_DONE_OK : MB_UPGR_STATUS_DONE_ERROR;
    }
    mbu.regs.writeDone = 0;
  }

  return ret;
}


Status_t MbUpgr_WriteRegisters(uint16_t address, uint16_t count, const uint8_t *data)
{
  uint8_t *storage;
  uint16_t i;

  if (!MbUpgr_InRange(address, count))
  {
    return STATUS_ERROR;
  }
  storage = (uint8_t *)&mbu.regs + (address - MB_UPGR_BASE_ADDRESS) * 2;

  for (i = 0; i < count; i++)
  {
    storage[i * 2 + 1] = data[i * 2 + 0];
    storage[i * 2 + 0] = data[i * 2 + 1];
  }

  if (address - MB_UPGR_BASE_ADDRESS < 4)
  {
    mbu.headerWritten = true;
  }
  /* Busy from the write-done request until the page is programmed (no stale READY) */
  if ((uint32_t)(address - MB_UPGR_BASE_ADDRESS) <= MB_UPGR_WRITE_DONE_REG
      && (uint32_t)(address - MB_UPGR_BASE_ADDRESS + count) > MB_UPGR_WRITE_DONE_REG
      && mbu.regs.writeDone != 0)
  {
    mbu.regs.status = MB_UPGR_STATUS_BUSY;
  }

  return STATUS_OK;
}


Status_t MbUpgr_ReadRegisters(uint16_t address, uint16_t count, uint8_t *data)
{
  const uint8_t *storage;
  uint16_t i;

  if (!MbUpgr_InRange(address, count))
  {
    return STATUS_ERROR;
  }
  storage = (const uint8_t *)&mbu.regs + (address - MB_UPGR_BASE_ADDRESS) * 2;

  for (i = 0; i < count; i++)
  {
    data[i * 2 + 1] = storage[i * 2 + 0];
    data[i * 2 + 0] = storage[i * 2 + 1];
  }

  return STATUS_OK;
}


uint16_t MbUpgr_GetType(void)
{
  return mbu.regs.type;
}


uint16_t MbUpgr_GetMode(void)
{
  return mbu.regs.mode;
}

#endif /* REGMAP_LIB_UPGRADE */

/** @} */
