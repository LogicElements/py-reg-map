/**
 * @file       port_stm32.c
 * @brief      STM32 HAL port of the regmap library: tick, critical section, callbacks
 *
 * Port file: written once by `regmap export-lib`, then owned by the project.
 */

#include "port.h"
#include "port_stm32.h"

static uint32_t criticalPrimask;
static uint32_t criticalNesting;

uint32_t Port_GetTickMs(void)
{
  return HAL_GetTick();
}


void Port_CriticalEnter(void)
{
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  if (criticalNesting++ == 0u)
  {
    criticalPrimask = primask;
  }
}


void Port_CriticalExit(void)
{
  if (criticalNesting > 0u && --criticalNesting == 0u)
  {
    __set_PRIMASK(criticalPrimask);
  }
}

#if REGMAP_LIB_MODBUS
void MbPort_NotifyFromIsr(void)
{
  /* FreeRTOS: wake the task calling MbSlave_Handle, e.g. vTaskNotifyGiveFromISR(...) */
}
#endif

#if REGMAP_LIB_LEBIN
void LebinPort_NotifyFromIsr(void)
{
  /* FreeRTOS: wake the task calling Lebin_Handle, e.g. vTaskNotifyGiveFromISR(...) */
}
#endif


void PortStm32_UartRxEvent(UART_HandleTypeDef *huart, uint16_t size)
{
#if REGMAP_LIB_MODBUS
  MbPortStm32_RxEvent(huart, size);
#endif
#if REGMAP_LIB_LEBIN && (LEBIN_PORT_TRANSPORT == LEBIN_PORT_UART)
  LebinPortUart_RxEvent(huart, size);
#endif
  UNUSED(huart);
  UNUSED(size);
}


void PortStm32_UartTxCplt(UART_HandleTypeDef *huart)
{
#if REGMAP_LIB_MODBUS
  MbPortStm32_TxCplt(huart);
#endif
  UNUSED(huart);
}


void PortStm32_UartError(UART_HandleTypeDef *huart)
{
#if REGMAP_LIB_MODBUS
  MbPortStm32_Error(huart);
#endif
#if REGMAP_LIB_LEBIN && (LEBIN_PORT_TRANSPORT == LEBIN_PORT_UART)
  LebinPortUart_Error(huart);
#endif
  UNUSED(huart);
}

#if REGMAP_LIB_HAL_UART_CALLBACKS
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
  PortStm32_UartRxEvent(huart, Size);
}


void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  PortStm32_UartTxCplt(huart);
}


void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  PortStm32_UartError(huart);
}
#endif
