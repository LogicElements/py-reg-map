/**
 * @file       mb_rtu_app.h
 * @version    $(APP_VERSION)
 * @date       $(RELEASE_DATE)
 * @brief      Modbus RTU application implementation
 * @author     jan.bartovsky
 *
 * @copyright  Logic Elements Copyright
 *
 * @defgroup grMbRtuApp Modbus RTU server application
 * @{
 * @brief Application callbacks for Modbus RTU server
 *
 * This module contains callbacks for reading and writing registers.
 *
 * @par Main features:
 *
 */
#ifndef MB_RTU_APP_H_
#define MB_RTU_APP_H_

/* Includes ------------------------------------------------------------------*/

#include "common.h"


/* Definitions----------------------------------------------------------------*/


/**
 * Return value for registers where read is not permitted
 */
#define MB_READ_NOT_SUPPORTED             0xFFFF


/** @defgroup grMbRegInput
 *  @ingroup grMbRtuApp
 *  @brief Input register addresses
 *  @{
 */

#define MB_INPUT_FIRST    0

#define MB_INPUT_SYS_UPTIME_0              0u
#define MB_INPUT_SYS_UPTIME_1              1u
#define MB_INPUT_SYS_REGMAP_VERSION_0      2u
#define MB_INPUT_SYS_REGMAP_VERSION_1      3u
#define MB_INPUT_SYS_STATUS_0              4u
#define MB_INPUT_SYS_STATUS_1              5u
#define MB_INPUT_SYS_IO_INPUT              6u
#define MB_INPUT_FACT_SERIAL_NUMBER_0      7u
#define MB_INPUT_FACT_SERIAL_NUMBER_1      8u
#define MB_INPUT_FACT_DEVICE_ID_0          9u
#define MB_INPUT_FACT_DEVICE_ID_1          10u
#define MB_INPUT_FACT_HW_REVISION_0        11u
#define MB_INPUT_FACT_HW_REVISION_1        12u
#define MB_INPUT_FACT_BOOT_REVISION_0      13u
#define MB_INPUT_FACT_BOOT_REVISION_1      14u
#define MB_INPUT_FIRM_REVISION_0           15u
#define MB_INPUT_FIRM_REVISION_1           16u
#define MB_INPUT_FIRM_ASSEMBLY_INFO_0      17u
#define MB_INPUT_FIRM_ASSEMBLY_INFO_1      18u
#define MB_INPUT_FIRM_APP_CHECKSUM_0       19u
#define MB_INPUT_FIRM_APP_CHECKSUM_1       20u
#define MB_INPUT_FIRM_APP_SIZE_0           21u
#define MB_INPUT_FIRM_APP_SIZE_1           22u
#define MB_INPUT_DBG_WRITES_CONF_0         23u
#define MB_INPUT_DBG_WRITES_CONF_1         24u

#define MB_INPUT_LAST     24


/** @} */


/** @defgroup grMbRegHolding
 *  @ingroup grMbRtuApp
 *  @brief Holding register addresses
 *  @{
 */

#define MB_HOLD_FIRST     0

#define MB_HOLD_SYS_COMMAND_0              0u
#define MB_HOLD_SYS_COMMAND_1              1u
#define MB_HOLD_SYS_TEST_0                 2u
#define MB_HOLD_SYS_TEST_1                 3u
#define MB_HOLD_COM_MB_BAUD_RATE           4u
#define MB_HOLD_COM_MB_PARITY              5u
#define MB_HOLD_COM_MB_STOP_BITS           6u
#define MB_HOLD_COM_MB_APPLY               7u
#define MB_HOLD_COM_MB_TIMEOUT             8u
#define MB_HOLD_CALIB_MODE                 9u
#define MB_HOLD_CALIB_RESERVED             10u
#define MB_HOLD_AMP_GAIN                   100u
#define MB_HOLD_AMP_LOAD                   101u

#define MB_HOLD_LAST      101


/** @} */

/* Typedefs-------------------------------------------------------------------*/

/* Functions -----------------------------------------------------------------*/

/**
 * Application callback for reading holding registers one by one.
 *
 * @param address Address of the register
 * @param value Pointer where register value should be stored
 * @return Status
 */
Status_t MbRtu_ReadHoldingRegCallback(uint16_t address, uint16_t *value);

/**
 * Application callback for reading input registers one by on.
 * @param address Address of the register
 * @param value Value where register value should be stored.
 * @return Status
 */
Status_t MbRtu_ReadInputRegCallback(uint16_t address, uint16_t *value);

/**
 * Application callback for writing holding register one by one
 * @param address Address of the register to write
 * @param value New value of the register
 * @return Status
 */
Status_t MbRtu_WriteHoldingRegCallback(uint16_t address, uint16_t value);



#endif /* MB_RTU_APP_H_ */
/** @} */
