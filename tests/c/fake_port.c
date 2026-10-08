#include "fake_port.h"

/* General -------------------------------------------------------------------*/

uint32_t fake_tick = 1000;
const uint8_t fake_bootloader_image[64];
const uint8_t fake_application_image[64];

uint32_t Port_GetTickMs(void)
{
  return fake_tick;
}

uint32_t HAL_GetTick(void)
{
  return fake_tick;
}

void Port_CriticalEnter(void)
{
}

void Port_CriticalExit(void)
{
}

/* Modbus --------------------------------------------------------------------*/

MbSlave_Config_t fake_mb_cfg;
int fake_mb_init_count;
uint8_t fake_mb_tx[MBSLAVE_FRAME_MAX];
uint16_t fake_mb_tx_len;
int fake_mb_tx_count;

Status_t MbPort_Init(const MbSlave_Config_t *cfg)
{
  fake_mb_cfg = *cfg;
  fake_mb_init_count++;
  return STATUS_OK;
}

Status_t MbPort_Send(const uint8_t *data, uint16_t len)
{
  memcpy(fake_mb_tx, data, len);
  fake_mb_tx_len = len;
  fake_mb_tx_count++;
  return STATUS_OK;
}

void MbPort_NotifyFromIsr(void)
{
}

void fake_mb_reset(void)
{
  fake_mb_init_count = 0;
  fake_mb_tx_len = 0;
  fake_mb_tx_count = 0;
}

/* LeBin ---------------------------------------------------------------------*/

bool fake_lebin_busy;
uint8_t fake_lebin_tx[LEBIN_PACKET_MAX];
uint16_t fake_lebin_tx_len;
int fake_lebin_tx_count;
int fake_lebin_init_count;

Status_t LebinPort_Init(void)
{
  fake_lebin_init_count++;
  return STATUS_OK;
}

bool LebinPort_TxReady(void)
{
  return !fake_lebin_busy;
}

Status_t LebinPort_Send(const uint8_t *data, uint16_t len)
{
  if (fake_lebin_busy)
  {
    return STATUS_BUSY;
  }
  memcpy(fake_lebin_tx, data, len);
  fake_lebin_tx_len = len;
  fake_lebin_tx_count++;
  return STATUS_OK;
}

void LebinPort_NotifyFromIsr(void)
{
}

void fake_lebin_reset(void)
{
  fake_lebin_busy = false;
  fake_lebin_tx_len = 0;
  fake_lebin_tx_count = 0;
  fake_lebin_init_count = 0;
}

/* Upgrade -------------------------------------------------------------------*/

uint8_t fake_flash[FAKE_FLASH_SIZE];
int fake_upgrade_begin_count;
uint32_t fake_upgrade_begin_size;
int fake_verify_count;
uint32_t fake_verify_size;
Status_t fake_verify_result;
int fake_apply_count;

Status_t UpgradePort_Begin(uint32_t size)
{
  fake_upgrade_begin_count++;
  fake_upgrade_begin_size = size;
  memset(fake_flash, 0xFF, sizeof(fake_flash));
  return (size <= FAKE_FLASH_SIZE) ? STATUS_OK : STATUS_ERROR;
}

Status_t UpgradePort_Write(uint32_t offset, const uint8_t *data, uint32_t len)
{
  /* Port contract: offset and len are multiples of UPGRADE_WRITE_UNIT */
  if (offset % 32u != 0 || len % 32u != 0 || len > FAKE_FLASH_SIZE || offset > FAKE_FLASH_SIZE - len)
  {
    return STATUS_ERROR;
  }
  memcpy(fake_flash + offset, data, len);
  return STATUS_OK;
}

Status_t UpgradePort_Verify(uint32_t size)
{
  fake_verify_count++;
  fake_verify_size = size;
  return fake_verify_result;
}

void UpgradePort_Apply(void)
{
  fake_apply_count++;
}

void fake_upgrade_reset(void)
{
  fake_upgrade_begin_count = 0;
  fake_upgrade_begin_size = 0;
  fake_verify_count = 0;
  fake_verify_size = 0;
  fake_verify_result = STATUS_OK;
  fake_apply_count = 0;
}
