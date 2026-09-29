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

    case MB_INPUT_SYS_UPTIME_0:
      *value = *((uint16_t *)CONF_PTR(CONF_SYS_UPTIME) + 0);
      break;
    case MB_INPUT_SYS_UPTIME_1:
      *value = *((uint16_t *)CONF_PTR(CONF_SYS_UPTIME) + 1);
      break;
    case MB_INPUT_SYS_REGMAP_VERSION_0:
      *value = *((uint16_t *)CONF_PTR(CONF_SYS_REGMAP_VERSION) + 0);
      break;
    case MB_INPUT_SYS_REGMAP_VERSION_1:
      *value = *((uint16_t *)CONF_PTR(CONF_SYS_REGMAP_VERSION) + 1);
      break;
    case MB_INPUT_SYS_STATUS_0:
      *value = *((uint16_t *)CONF_PTR(CONF_SYS_STATUS) + 0);
      break;
    case MB_INPUT_SYS_STATUS_1:
      *value = *((uint16_t *)CONF_PTR(CONF_SYS_STATUS) + 1);
      break;
    case MB_INPUT_SYS_IO_INPUT:
      *value = conf.sys.io_input;
      break;
    case MB_INPUT_FACT_SERIAL_NUMBER_0:
      *value = *((uint16_t *)CONF_PTR(CONF_FACT_SERIAL_NUMBER) + 0);
      break;
    case MB_INPUT_FACT_SERIAL_NUMBER_1:
      *value = *((uint16_t *)CONF_PTR(CONF_FACT_SERIAL_NUMBER) + 1);
      break;
    case MB_INPUT_FACT_DEVICE_ID_0:
      *value = *((uint16_t *)CONF_PTR(CONF_FACT_DEVICE_ID) + 0);
      break;
    case MB_INPUT_FACT_DEVICE_ID_1:
      *value = *((uint16_t *)CONF_PTR(CONF_FACT_DEVICE_ID) + 1);
      break;
    case MB_INPUT_FACT_HW_REVISION_0:
      *value = *((uint16_t *)CONF_PTR(CONF_FACT_HW_REVISION) + 0);
      break;
    case MB_INPUT_FACT_HW_REVISION_1:
      *value = *((uint16_t *)CONF_PTR(CONF_FACT_HW_REVISION) + 1);
      break;
    case MB_INPUT_FACT_BOOT_REVISION_0:
      *value = *((uint16_t *)CONF_PTR(CONF_FACT_BOOT_REVISION) + 0);
      break;
    case MB_INPUT_FACT_BOOT_REVISION_1:
      *value = *((uint16_t *)CONF_PTR(CONF_FACT_BOOT_REVISION) + 1);
      break;
    case MB_INPUT_FIRM_REVISION_0:
      *value = *((uint16_t *)CONF_PTR(CONF_FIRM_REVISION) + 0);
      break;
    case MB_INPUT_FIRM_REVISION_1:
      *value = *((uint16_t *)CONF_PTR(CONF_FIRM_REVISION) + 1);
      break;
    case MB_INPUT_FIRM_ASSEMBLY_INFO_0:
      *value = *((uint16_t *)CONF_PTR(CONF_FIRM_ASSEMBLY_INFO) + 0);
      break;
    case MB_INPUT_FIRM_ASSEMBLY_INFO_1:
      *value = *((uint16_t *)CONF_PTR(CONF_FIRM_ASSEMBLY_INFO) + 1);
      break;
    case MB_INPUT_FIRM_APP_CHECKSUM_0:
      *value = *((uint16_t *)CONF_PTR(CONF_FIRM_APP_CHECKSUM) + 0);
      break;
    case MB_INPUT_FIRM_APP_CHECKSUM_1:
      *value = *((uint16_t *)CONF_PTR(CONF_FIRM_APP_CHECKSUM) + 1);
      break;
    case MB_INPUT_FIRM_APP_SIZE_0:
      *value = *((uint16_t *)CONF_PTR(CONF_FIRM_APP_SIZE) + 0);
      break;
    case MB_INPUT_FIRM_APP_SIZE_1:
      *value = *((uint16_t *)CONF_PTR(CONF_FIRM_APP_SIZE) + 1);
      break;
    case MB_INPUT_DBG_WRITES_CONF_0:
      *value = *((uint16_t *)CONF_PTR(CONF_DBG_WRITES_CONF) + 0);
      break;
    case MB_INPUT_DBG_WRITES_CONF_1:
      *value = *((uint16_t *)CONF_PTR(CONF_DBG_WRITES_CONF) + 1);
      break;


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

    case MB_HOLD_SYS_COMMAND_0:
      *value = *((uint16_t *)CONF_PTR(CONF_SYS_COMMAND) + 0);
      break;
    case MB_HOLD_SYS_COMMAND_1:
      *value = *((uint16_t *)CONF_PTR(CONF_SYS_COMMAND) + 1);
      break;
    case MB_HOLD_SYS_TEST_0:
      *value = *((uint16_t *)CONF_PTR(CONF_SYS_TEST) + 0);
      break;
    case MB_HOLD_SYS_TEST_1:
      *value = *((uint16_t *)CONF_PTR(CONF_SYS_TEST) + 1);
      break;
    case MB_HOLD_COM_MB_BAUD_RATE:
      *value = conf.com.mb_baud_rate;
      break;
    case MB_HOLD_COM_MB_PARITY:
      *value = conf.com.mb_parity;
      break;
    case MB_HOLD_COM_MB_STOP_BITS:
      *value = conf.com.mb_stop_bits;
      break;
    case MB_HOLD_COM_MB_APPLY:
      *value = conf.com.mb_apply;
      break;
    case MB_HOLD_COM_MB_TIMEOUT:
      *value = conf.com.mb_timeout;
      break;
    case MB_HOLD_CALIB_MODE:
      *value = conf.calib.mode;
      break;
    case MB_HOLD_CALIB_RESERVED:
      *value = conf.calib.reserved;
      break;
    case MB_HOLD_AMP_GAIN:
      *value = conf.amp.gain;
      break;
    case MB_HOLD_AMP_LOAD:
      *value = conf.amp.load;
      break;


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

    case MB_HOLD_SYS_COMMAND_0:
      *((uint16_t *)CONF_PTR(CONF_SYS_COMMAND) + 0) = value;
      break;
    case MB_HOLD_SYS_COMMAND_1:
      *((uint16_t *)CONF_PTR(CONF_SYS_COMMAND) + 1) = value;
      id = CONF_SYS_COMMAND;
      break;
    case MB_HOLD_SYS_TEST_0:
      *((uint16_t *)CONF_PTR(CONF_SYS_TEST) + 0) = value;
      break;
    case MB_HOLD_SYS_TEST_1:
      *((uint16_t *)CONF_PTR(CONF_SYS_TEST) + 1) = value;
      id = CONF_SYS_TEST;
      break;
    case MB_HOLD_COM_MB_BAUD_RATE:
      conf.com.mb_baud_rate = (com_mb_baud_rate_t)value;
      id = CONF_COM_MB_BAUD_RATE;
      break;
    case MB_HOLD_COM_MB_PARITY:
      conf.com.mb_parity = (com_mb_parity_t)value;
      id = CONF_COM_MB_PARITY;
      break;
    case MB_HOLD_COM_MB_STOP_BITS:
      conf.com.mb_stop_bits = (com_mb_stop_bits_t)value;
      id = CONF_COM_MB_STOP_BITS;
      break;
    case MB_HOLD_COM_MB_APPLY:
      conf.com.mb_apply = value;
      id = CONF_COM_MB_APPLY;
      break;
    case MB_HOLD_COM_MB_TIMEOUT:
      conf.com.mb_timeout = value;
      id = CONF_COM_MB_TIMEOUT;
      break;
    case MB_HOLD_CALIB_MODE:
      conf.calib.mode = value;
      id = CONF_CALIB_MODE;
      break;
    case MB_HOLD_CALIB_RESERVED:
      conf.calib.reserved = value;
      id = CONF_CALIB_RESERVED;
      break;
    case MB_HOLD_AMP_GAIN:
      conf.amp.gain = (amp_gain_t)value;
      id = CONF_AMP_GAIN;
      break;
    case MB_HOLD_AMP_LOAD:
      conf.amp.load = (amp_load_t)value;
      id = CONF_AMP_LOAD;
      break;


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
