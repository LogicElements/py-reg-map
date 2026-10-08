/**
 * @file       configuration.c
 * @brief      Configuration and tools for register map access
 * @regmap-lib
 * @addtogroup grConfig
 * @{
 */

/* Includes ------------------------------------------------------------------*/

#include "configuration.h"
#include "config_app.h"
#include "lib_bytes.h"

/* Private macros ------------------------------------------------------------*/

#ifndef CONF_SECTION
#define CONF_SECTION(name) __attribute__((section(name)))
#endif

/* Constants -----------------------------------------------------------------*/

/** Firmware information block in the flash memory */
const uint32_t CONF_SECTION(".sectionFwInfo") CONF_FIRMWARE_INFO[8] = {
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};

/** Empty (default) firmware information block, used for comparison */
const uint32_t CONF_SECTION(".sectionEndOfFlash") CONF_FIRMWARE_INFO_DEFAULT[8] = {
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};

/** Register lengths by the length code in the ID */
const uint32_t CONF_LENGTH[16] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192,
                                  16384, 32768};

/* Private variables ---------------------------------------------------------*/

#if CONF_REG_SYNCED_NUMBER > 0
/** Last synchronised values */
static uint8_t CONF_REG_SYN_LOCAL[CONF_REG_LOCAL_LENGTH];
#endif

/* Functions -----------------------------------------------------------------*/

Status_t Config_Init(void)
{
  if (CONF_DIM_CONDITION)
  {
    return STATUS_ERROR;
  }

  RegMap_RestoreFactoryValues();

  return Config_AppInit();
}


Status_t Config_CheckLimits(uint32_t id)
{
  Status_t ret = STATUS_ERROR;

  if (CONF_BLOCK_ID(id) < CONF_REG_BLOCK_NUMBER)
  {
    if (CONF_REG_LIMIT[CONF_BLOCK_ID(id)] >= (CONF_ADDR_ID(id) + CONF_BYTE_LEN_ID(id)))
    {
      ret = STATUS_OK;
    }
  }

  return ret;
}


Status_t Config_ApplyConfig(uint32_t id)
{
  return Config_Callback(id);
}


Status_t Config_ReadStream(const uint8_t *data, uint32_t length)
{
  uint32_t idx = 0;
  uint32_t id;
  uint32_t size;
  uint32_t i;
  bool known;

  /* First entry: map version with the same major part as the factory value */
  if (length < 8 || Bytes_GetU32Le(data) != CONF_SYS_REGMAP_VERSION
      || (Bytes_GetU32Le(data + 4) & 0xFFFF0000u)
         != (CONF_INT(CONF_SYS_REGMAP_VERSION) & 0xFFFF0000u))
  {
    return STATUS_ERROR;
  }

  while (idx + 4 <= length)
  {
    id = Bytes_GetU32Le(data + idx);
    size = CONF_BYTE_LEN_ID(id);
    if (idx + 4 + size > length)
    {
      return STATUS_ERROR;
    }

    known = false;
    for (i = 0; i != CONF_REG_FLASH_NUMBER; i++)
    {
      if (CONF_REG_FLASH[i] == id)
      {
        known = true;
      }
    }
    if (known)
    {
      memcpy(CONF_PTR(id), data + idx + 4, size);
    }

    idx += 4 + size;
  }

  return STATUS_OK;
}


Status_t Config_FillStream(uint8_t *data, uint32_t *length, uint32_t maxLength)
{
  Status_t ret = STATUS_OK;
  uint32_t idx = 0;
  uint32_t id;
  uint32_t size;
  uint32_t i;

  for (i = 0; i != CONF_REG_FLASH_NUMBER; i++)
  {
    id = CONF_REG_FLASH[i];
    size = CONF_BYTE_LEN_ID(id);
    if (idx + 4 + size > maxLength)
    {
      ret = STATUS_ERROR;
      break;
    }
    Bytes_PutU32Le(data + idx, id);
    memcpy(data + idx + 4, CONF_PTR(id), size);
    idx += 4 + size;
  }

  *length = idx;
  return ret;
}


Status_t Config_NeedToSync(uint8_t *data, uint16_t *length)
{
  Status_t ret = STATUS_ERROR;
  uint32_t packetIdx = 0;
#if CONF_REG_SYNCED_NUMBER > 0
  uint32_t tempIdx = 0;
  uint32_t id;
  uint32_t size;
  uint32_t i;

  for (i = 0; i != CONF_REG_SYNCED_NUMBER; i++)
  {
    id = CONF_REG_SYNCED[i];
    size = CONF_BYTE_LEN_ID(id);
    if (memcmp(CONF_PTR(id), CONF_REG_SYN_LOCAL + tempIdx, size) != 0)
    {
      Bytes_PutU32Le(data + packetIdx, id);
      memcpy(data + packetIdx + 4, CONF_PTR(id), size);
      memcpy(CONF_REG_SYN_LOCAL + tempIdx, CONF_PTR(id), size);
      packetIdx += 4 + size;
      ret = STATUS_OK;
    }
    tempIdx += size;
  }
#else
  UNUSED(data);
#endif

  *length = (uint16_t)packetIdx;
  return ret;
}

/** @} */
