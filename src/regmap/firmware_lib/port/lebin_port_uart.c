/**
 * @file       lebin_port_uart.c
 * @brief      LeBin port for the STM32 HAL UART
 *
 * Port file: written once by `regmap export-lib`, then owned by the project. Used when
 * LEBIN_PORT_TRANSPORT is LEBIN_PORT_UART; the UART is configured in CubeMX (IRQ enabled).
 */

#include "port.h"
#include "port_stm32.h"

#if REGMAP_LIB_LEBIN && (LEBIN_PORT_TRANSPORT == LEBIN_PORT_UART)

#include "lebin.h"

extern UART_HandleTypeDef LEBIN_PORT_UART_HANDLE;

static uint8_t lebinRxBuffer[64];

static void LebinPortUart_StartReceive(void)
{
  (void)HAL_UARTEx_ReceiveToIdle_IT(&LEBIN_PORT_UART_HANDLE, lebinRxBuffer,
                                    sizeof(lebinRxBuffer));
}


Status_t LebinPort_Init(void)
{
  LebinPortUart_StartReceive();
  return STATUS_OK;
}


bool LebinPort_TxReady(void)
{
  return LEBIN_PORT_UART_HANDLE.gState == HAL_UART_STATE_READY;
}


Status_t LebinPort_Send(const uint8_t *data, uint16_t len)
{
  return (HAL_UART_Transmit_IT(&LEBIN_PORT_UART_HANDLE, (uint8_t *)data, len) == HAL_OK)
         ? STATUS_OK : STATUS_BUSY;
}


void LebinPortUart_RxEvent(UART_HandleTypeDef *huart, uint16_t size)
{
  if (huart != &LEBIN_PORT_UART_HANDLE)
  {
    return;
  }
  if (size > 0u)
  {
    Lebin_RxBytes(lebinRxBuffer, size);
  }
  LebinPortUart_StartReceive();
}


void LebinPortUart_Error(UART_HandleTypeDef *huart)
{
  if (huart != &LEBIN_PORT_UART_HANDLE)
  {
    return;
  }
  (void)HAL_UART_AbortReceive(huart);
  LebinPortUart_StartReceive();
}

#endif
