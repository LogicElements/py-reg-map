/**
 * @file       upgrade_port_stm32.c
 * @brief      Firmware upgrade port: STM32 internal flash
 *
 * Port file: written once by `regmap export-lib`, then owned by the project.
 * Implemented for STM32H7 (sectors, 256-bit flash words) and for page-based families with
 * double-word programming and a page index (G0, G4, L4, L5, U5 ...). Other families: adapt
 * UpgradePortStm32_EraseUnit and UpgradePortStm32_Program (doc/firmware-porting.md).
 */

#include "port.h"

#if REGMAP_LIB_UPGRADE

#include "configuration.h"
#include "fw_upgrade.h"

#if defined(FLASH_TYPEPROGRAM_FLASHWORD)
/* STM32H7 */
#define UPGRADE_PORT_ERASE_UNIT     FLASH_SECTOR_SIZE
#define UPGRADE_PORT_PROGRAM_SIZE   (FLASH_NB_32BITWORD_IN_FLASHWORD * 4u)
#elif defined(FLASH_TYPEERASE_PAGES) && defined(FLASH_PAGE_SIZE) \
      && defined(FLASH_TYPEPROGRAM_DOUBLEWORD)
/* Page-based families */
#define UPGRADE_PORT_ERASE_UNIT     FLASH_PAGE_SIZE
#define UPGRADE_PORT_PROGRAM_SIZE   8u
#else
#error "Implement flash erase/program for this STM32 family (see doc/firmware-porting.md)"
#endif

#if (UPGRADE_WRITE_UNIT % UPGRADE_PORT_PROGRAM_SIZE) != 0
#error "UPGRADE_WRITE_UNIT must be a multiple of the flash programming unit"
#endif

/** Area [0, erasedEnd) of the upgrade area is erased */
static uint32_t erasedEnd;

/** Aligned copy of one programming unit (the source may be unaligned) */
static uint8_t programBuffer[UPGRADE_PORT_PROGRAM_SIZE] __attribute__((aligned(8)));

static Status_t UpgradePortStm32_EraseUnit(uint32_t address)
{
  FLASH_EraseInitTypeDef erase;
  uint32_t error = 0;

  memset(&erase, 0, sizeof(erase));
#if defined(FLASH_TYPEPROGRAM_FLASHWORD)
  erase.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase.NbSectors = 1;
  erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
#if defined(DUAL_BANK)
  if (address >= FLASH_BANK2_BASE)
  {
    erase.Banks = FLASH_BANK_2;
    erase.Sector = (address - FLASH_BANK2_BASE) / FLASH_SECTOR_SIZE;
  }
  else
#endif
  {
    erase.Banks = FLASH_BANK_1;
    erase.Sector = (address - FLASH_BANK1_BASE) / FLASH_SECTOR_SIZE;
  }
#else
  erase.TypeErase = FLASH_TYPEERASE_PAGES;
  erase.NbPages = 1;
#if defined(FLASH_BANK_2) && defined(FLASH_BANK_SIZE)
  erase.Banks = ((address - FLASH_BASE) >= FLASH_BANK_SIZE) ? FLASH_BANK_2 : FLASH_BANK_1;
  erase.Page = ((address - FLASH_BASE) % FLASH_BANK_SIZE) / FLASH_PAGE_SIZE;
#else
  erase.Banks = FLASH_BANK_1;
  erase.Page = (address - FLASH_BASE) / FLASH_PAGE_SIZE;
#endif
#endif

  return (HAL_FLASHEx_Erase(&erase, &error) == HAL_OK) ? STATUS_OK : STATUS_ERROR;
}


static Status_t UpgradePortStm32_Program(uint32_t address, const uint8_t *data)
{
  HAL_StatusTypeDef status;

  memcpy(programBuffer, data, UPGRADE_PORT_PROGRAM_SIZE);
#if defined(FLASH_TYPEPROGRAM_FLASHWORD)
  status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, address,
                             (uint32_t)(uintptr_t)programBuffer);
#else
  {
    uint64_t value;

    memcpy(&value, programBuffer, sizeof(value));
    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, value);
  }
#endif

  return (status == HAL_OK) ? STATUS_OK : STATUS_ERROR;
}


/** Erase erase units until [0, end) is erased */
static Status_t UpgradePortStm32_EnsureErased(uint32_t end)
{
  Status_t ret = STATUS_OK;

  (void)HAL_FLASH_Unlock();
  while (ret == STATUS_OK && erasedEnd < end)
  {
    ret = UpgradePortStm32_EraseUnit(UPGRADE_PORT_AREA_ADDRESS + erasedEnd);
    erasedEnd += UPGRADE_PORT_ERASE_UNIT;
  }
  (void)HAL_FLASH_Lock();

  return ret;
}


Status_t UpgradePort_Begin(uint32_t size)
{
  erasedEnd = 0;
  if (size > UPGRADE_PORT_AREA_SIZE)
  {
    return STATUS_ERROR;
  }

  return (size > 0u) ? UpgradePortStm32_EnsureErased(size) : STATUS_OK;
}


Status_t UpgradePort_Write(uint32_t offset, const uint8_t *data, uint32_t len)
{
  Status_t ret;
  uint32_t i;

  if (offset % UPGRADE_PORT_PROGRAM_SIZE != 0u || len % UPGRADE_PORT_PROGRAM_SIZE != 0u
      || len > UPGRADE_PORT_AREA_SIZE || offset > UPGRADE_PORT_AREA_SIZE - len)
  {
    return STATUS_ERROR;
  }

  ret = UpgradePortStm32_EnsureErased(offset + len);

  (void)HAL_FLASH_Unlock();
  for (i = 0; ret == STATUS_OK && i < len; i += UPGRADE_PORT_PROGRAM_SIZE)
  {
    ret = UpgradePortStm32_Program(UPGRADE_PORT_AREA_ADDRESS + offset + i, data + i);
  }
  (void)HAL_FLASH_Lock();

  return ret;
}


Status_t UpgradePort_Verify(uint32_t size)
{
  UNUSED(size);
  return UPGRADE_PORT_VERIFY(UPGRADE_PORT_AREA_ADDRESS, size);
}


void UpgradePort_Apply(void)
{
  /* Request the switch to the new image here, e.g. set a flag for a restart into the
   * bootloader after the Modbus response has been sent. */
}

#endif /* REGMAP_LIB_UPGRADE */
