/**
 * @file       config_app.c
 * @brief      Application hooks of the configuration module
 *
 * Port file: written once by `regmap export-lib`, then owned by the project. The Modbus part
 * assumes the COM registers and STAT_BIT_MB_TIMEOUT of the `regmap init` starter map.
 */

#include "config_app.h"
#include "configuration.h"

#if REGMAP_LIB_MODBUS
/** Baud rates selected by the MB_BAUD_RATE enumeration */
static const uint32_t baudRates[] = {9600, 19200, 38400, 57600, 115200};
#endif

Status_t Config_AppInit(void)
{
  /* Restore stored registers here, e.g. Config_ReadStream() on the flash copy */
  return STATUS_OK;
}


Status_t Config_Callback(uint32_t id)
{
#if REGMAP_LIB_MODBUS
  MbSlave_Config_t cfg;

  /* Writing 1 to MB_APPLY applies the Modbus settings after the current response */
  if (id == CONF_COM_MB_APPLY && conf.com.mb_apply != 0)
  {
    conf.com.mb_apply = 0;
    ConfigApp_ModbusConfig(&cfg);
    (void)MbSlave_Reconfigure(&cfg);
  }
#else
  UNUSED(id);
#endif

  /* Store flash registers here: (id & 0x070) == 0x070 */
  return STATUS_OK;
}

#if REGMAP_LIB_MODBUS

void ConfigApp_ModbusConfig(MbSlave_Config_t *cfg)
{
  uint32_t baud = (uint32_t)conf.com.mb_baud_rate;

  /* Enumeration index, or the baud rate itself when the register holds a number */
  cfg->baudRate = (baud < sizeof(baudRates) / sizeof(baudRates[0])) ? baudRates[baud] : baud;
  cfg->parity = (uint8_t)conf.com.mb_parity;
  cfg->stopBits = (conf.com.mb_stop_bits == MB_STOP_2) ? 2u : 1u;
  cfg->address = (uint8_t)conf.com.mb_address;
  cfg->timeoutMs = (uint32_t)conf.com.mb_timeout * 1000u;
}


void MbSlave_TimeoutChanged(bool timeout)
{
  if (timeout)
  {
    conf.sys.status |= STAT_BIT_MB_TIMEOUT;
  }
  else
  {
    conf.sys.status &= ~(uint32_t)STAT_BIT_MB_TIMEOUT;
  }
}

#endif /* REGMAP_LIB_MODBUS */
