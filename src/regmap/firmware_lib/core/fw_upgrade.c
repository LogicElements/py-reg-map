/**
 * @file       fw_upgrade.c
 * @brief      Firmware upgrade session shared by LeBin and Modbus
 * @regmap-lib
 */

#include "fw_upgrade.h"

#if REGMAP_LIB_UPGRADE

#include "port.h"

typedef struct
{
  bool active;        ///< Session running
  uint32_t received;  ///< End of the highest written data
} Upgrade_Private_t;

static Upgrade_Private_t upg;

Status_t Upgrade_Begin(uint32_t size)
{
  Status_t ret = UpgradePort_Begin(size);

  upg.active = (ret == STATUS_OK);
  upg.received = 0;

  return ret;
}


Status_t Upgrade_Write(uint32_t offset, const uint8_t *data, uint32_t len)
{
  uint8_t tail[UPGRADE_WRITE_UNIT];
  uint32_t full = len - len % UPGRADE_WRITE_UNIT;
  Status_t ret = STATUS_OK;

  if (!upg.active)
  {
    return STATUS_ERROR;
  }
  if (offset % UPGRADE_WRITE_UNIT != 0 || len > UINT32_MAX - UPGRADE_WRITE_UNIT - offset)
  {
    upg.active = false;
    return STATUS_ERROR;
  }

  if (full > 0)
  {
    ret = UpgradePort_Write(offset, data, full);
  }
  if (ret == STATUS_OK && full < len)
  {
    /* Short last chunk: pad to a full unit with erased-flash bytes */
    memset(tail, 0xFF, sizeof(tail));
    memcpy(tail, data + full, len - full);
    ret = UpgradePort_Write(offset + full, tail, UPGRADE_WRITE_UNIT);
  }
  if (ret != STATUS_OK)
  {
    upg.active = false;
    return STATUS_ERROR;
  }
  if (offset + len > upg.received)
  {
    upg.received = offset + len;
  }

  return STATUS_OK;
}


Status_t Upgrade_Finish(void)
{
  if (!upg.active)
  {
    return STATUS_ERROR;
  }
  upg.active = false;

  return UpgradePort_Verify(upg.received);
}


void Upgrade_Apply(void)
{
  UpgradePort_Apply();
}


bool Upgrade_IsActive(void)
{
  return upg.active;
}

#endif /* REGMAP_LIB_UPGRADE */
