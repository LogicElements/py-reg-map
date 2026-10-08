/**
 * @file       config_app.h
 * @brief      Application hooks of the configuration module
 *
 * Port file: written once by `regmap export-lib`, then owned by the project.
 */
#ifndef CONFIG_APP_H_
#define CONFIG_APP_H_

#include "common.h"
#include "regmap_lib_conf.h"

/**
 * Application initialisation after the factory values are set (e.g. read stored registers
 * from flash with Config_ReadStream).
 * @return Status
 */
Status_t Config_AppInit(void);

/**
 * Called by Config_ApplyConfig after a register was written (LeBin, Modbus, application).
 * @param id ID of the modified register
 * @return Status
 */
Status_t Config_Callback(uint32_t id);

#if REGMAP_LIB_MODBUS
#include "modbus_slave.h"

/**
 * Build the Modbus settings from the COM registers (MB_BAUD_RATE, MB_PARITY, MB_STOP_BITS,
 * MB_ADDRESS, MB_TIMEOUT of the `regmap init` starter map). Adapt when the map differs.
 * @param cfg Settings to fill
 */
void ConfigApp_ModbusConfig(MbSlave_Config_t *cfg);
#endif

#endif /* CONFIG_APP_H_ */
