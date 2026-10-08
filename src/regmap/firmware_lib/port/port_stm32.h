/**
 * @file       port_stm32.h
 * @brief      STM32 HAL port of the regmap library: internal interface
 *
 * Port file: written once by `regmap export-lib`, then owned by the project.
 */
#ifndef PORT_STM32_H_
#define PORT_STM32_H_

#include "common.h"
#include "regmap_lib_conf.h"

/* Dispatch of HAL UART callbacks to the library ports. Call them from your own HAL callbacks
 * when REGMAP_LIB_HAL_UART_CALLBACKS is 0. */
void PortStm32_UartRxEvent(UART_HandleTypeDef *huart, uint16_t size);
void PortStm32_UartTxCplt(UART_HandleTypeDef *huart);
void PortStm32_UartError(UART_HandleTypeDef *huart);

/* Modbus port (modbus_port_stm32.c) */
void MbPortStm32_RxEvent(UART_HandleTypeDef *huart, uint16_t size);
void MbPortStm32_TxCplt(UART_HandleTypeDef *huart);
void MbPortStm32_Error(UART_HandleTypeDef *huart);

/* LeBin UART port (lebin_port_uart.c) */
void LebinPortUart_RxEvent(UART_HandleTypeDef *huart, uint16_t size);
void LebinPortUart_Error(UART_HandleTypeDef *huart);

/* LeBin USB CDC port (lebin_port_usb_cdc.c): call from CDC_Receive_FS/HS in usbd_cdc_if.c */
void LebinPortUsb_Receive(uint8_t *buf, uint32_t len);

#endif /* PORT_STM32_H_ */
