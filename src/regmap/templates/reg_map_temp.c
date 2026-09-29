/**
 * @file       reg_map.c
 * @brief      Register map implementation
 * @addtogroup grRegMap
 * @{
 */

/* Includes ------------------------------------------------------------------*/

#include "reg_map.h"
#include "configuration.h"

/* Private defines -----------------------------------------------------------*/

/* Private macros  -----------------------------------------------------------*/

/* Private typedefs ----------------------------------------------------------*/

/* Private constants ---------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/** @defgroup grRegMapStorage
 *  @ingroup grRegMap
 *  @brief Definition of storage memory structure for all registers
 *  @{
 */

/**
 * Storage for the entire register map (structure of structures)
 */
/* < DEFINE REG MAP STORAGE > */

/** @} */

/* Private variables ---------------------------------------------------------*/
/* Public variables ----------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Functions -----------------------------------------------------------------*/


Status_t RegMap_RestoreFactoryValues(void)
{
  Status_t ret = STATUS_OK;
  //uint8_t *id = (uint8_t *)0x1FFF7A10;

  /* Copy firmware info */
  memcpy(&conf.firm, CONF_C_APPLICATION_OFFSET + (uint32_t)CONF_FW_INFO_OFFSET, sizeof(conf.firm));

  /* Copy Hardware info */
  memcpy(&conf.fact, CONF_C_BOOTLOADER_OFFSET + (uint32_t)CONF_FW_INFO_OFFSET, sizeof(conf.fact));

/* < REG MAP FACTORY > */

  return ret;
}


/** @} */
