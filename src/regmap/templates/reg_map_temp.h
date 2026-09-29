/**
 * @file       reg_map.h
 * @version    $(APP_VERSION)
 * @date       $(RELEASE_DATE)
 * @brief      Register map definition
 * @author     jan.bartovsky
 *
 * @copyright  Logic Elements Copyright
 *
 * @defgroup grRegMap Register map
 * @{
 * @brief Set of local register represented by 16-bit identifier
 *
 * This module contains definition and declaration of entire peripheral register map. It should
 * be generated from using excel sheet.
 *
 * @par Main features:
 * - Register map (set of register IDs)
 * - Storage memory structure for all registers
 * - Access macros to the registers according to its type
 * - Vectors of register that are to be logged, stored in EEPROM or synchronized
 * - Flash read and write API
 * - Callback for register change
 *
 * @par Example of initialization in the main().
 *
 * @code
 * //Configuration initial read out
 * Config_Init();
 * @endcode
 *
 *
 * @par Example of work with register.
 *
 * The snippet below shows how to set value of different registers. The call of ApplyConfig is here
 * to store the non-volatile register in the Flash memory.
 * @code
 * CONF_INT(REG_SYS_TIMESTAMP)++;
 * CONF_SHORT(REG_RF_ADDRESS_MY) = 0x01;
 * Config_ApplyConfig(REG_RF_ADDRESS_MY);
 * @endcode
 *
 */
#ifndef REG_MAP_H_
#define REG_MAP_H_

/* Includes ------------------------------------------------------------------*/

#include "common.h"

/* Definitions----------------------------------------------------------------*/


/** @defgroup grRegMapMap
 *  @ingroup grRegMap
 *  @brief Set of IDs of all registers in the map
 *  @{
 */

/* < DEFINE REG MAP > */

/** @} */

/** @defgroup grRegMapBits
 *  @ingroup grRegMap
 *  @brief Set of definitions of bits 
 *  @{
 */

/* < DEFINE REG BITS > */

/** @} */


/** @defgroup grRegMapConstants
 *  @ingroup grRegMap
 *  @brief Constant parameters of the register map
 *  @{
 */

/* < REG MAP PARAMS > */

/** @} */

/* Macros --------------------------------------------------------------------*/

/* Constants -------------------------------------------------------------------*/

/* Typedefs-------------------------------------------------------------------*/

/** @defgroup grRegMapTypedefs
 *  @ingroup grRegMap
 *  @brief Typedefs of structures that define register map
 *  @{
 */

/* < REG MAP TYPEDEFS > */

/** @} */

/* Functions -----------------------------------------------------------------*/


/**
 * Set factory values for all registers. 
 *
 * @return Status
 */
Status_t RegMap_RestoreFactoryValues(void);

#endif /* REG_MAP_H_ */
/** @} */
