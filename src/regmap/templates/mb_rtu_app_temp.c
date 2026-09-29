/**
 * @file       mb_rtu_app.c
 * @brief      Modbus RTU server implementation
 * @addtogroup grMbRtuApp
 * @{
 */

/* Includes ------------------------------------------------------------------*/

#include "mb_rtu_app.h"
#include "system_msp.h"

#include "configuration.h"

/* Private defines -----------------------------------------------------------*/
/* Private macros  -----------------------------------------------------------*/
/* Private typedefs ----------------------------------------------------------*/

/**
 * Declaration of all private variables
 */
typedef struct
{
}MbRtu_Private_t;

/* Private constants ---------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/

/**
 * Instance of all private variables (except HAL handles)
 */
static MbRtu_Private_t mb;

/* Private variables ---------------------------------------------------------*/
/* Public variables ----------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Functions -----------------------------------------------------------------*/


Status_t MbRtu_ReadInputRegCallback(uint16_t address, uint16_t *value)
{
  Status_t ret = STATUS_OK;

  UNUSED(mb);

  switch (address)
  {

/* < READ INPUT REG > */

    default:
      *value = MB_READ_NOT_SUPPORTED;
      ret = STATUS_ERROR;
      break;
  }

  /* Reverse byte order */
  *value = __REV16(*value);

  return ret;
}


Status_t MbRtu_ReadHoldingRegCallback(uint16_t address, uint16_t *value)
{
  Status_t ret = STATUS_OK;

  switch (address)
  {

/* < READ HOLD REG > */

    default:
      *value = MB_READ_NOT_SUPPORTED;
      ret = STATUS_ERROR;
      break;
  }

  /* Reverse byte order */
  *value = __REV16(*value);

  return ret;
}




Status_t MbRtu_WriteHoldingRegCallback(uint16_t address, uint16_t value)
{
  Status_t ret = STATUS_OK;
  uint32_t id = 0;

  switch (address)
  {

/* < WRITE HOLD REG > */

    default:
      ret = STATUS_ERROR;
      break;
  }

  if (id != 0)
  {
    Config_ApplyConfig(id);
  }

  return ret;
}

/* Private Functions ---------------------------------------------------------*/



/** @} */
