/**
 * @file       regmap_lib_conf.h
 * @brief      Settings of the regmap firmware library
 *
 * Port file: written once by `regmap export-lib`, then owned by the project. Every setting can
 * also be given on the compiler command line (-D...).
 */
#ifndef REGMAP_LIB_CONF_H_
#define REGMAP_LIB_CONF_H_

/* Modules: 1 = compiled in, 0 = left out ------------------------------------*/

#ifndef REGMAP_LIB_LEBIN
#define REGMAP_LIB_LEBIN              1
#endif
#ifndef REGMAP_LIB_MODBUS
#define REGMAP_LIB_MODBUS             1
#endif
#ifndef REGMAP_LIB_UPGRADE
#define REGMAP_LIB_UPGRADE            1
#endif

/* LeBin ---------------------------------------------------------------------*/

#ifndef LEBIN_RX_BUFFER_SIZE
#define LEBIN_RX_BUFFER_SIZE          4096u   ///< Received bytes not processed yet (power of two)
#endif
#ifndef LEBIN_PACKET_MAX
#define LEBIN_PACKET_MAX              4096u   ///< Largest request and response packet
#endif
#ifndef LEBIN_NEW_PACKET_MS
#define LEBIN_NEW_PACKET_MS           750u    ///< A partial packet older than this is dropped
#endif

#define LEBIN_PORT_USB_CDC            1
#define LEBIN_PORT_UART               2
#ifndef LEBIN_PORT_TRANSPORT
#define LEBIN_PORT_TRANSPORT          LEBIN_PORT_USB_CDC
#endif
#ifndef LEBIN_PORT_USB_DEVICE
#define LEBIN_PORT_USB_DEVICE         hUsbDeviceFS   ///< CubeMX USB device handle (hUsbDeviceHS for HS)
#endif
#ifndef LEBIN_PORT_UART_HANDLE
#define LEBIN_PORT_UART_HANDLE        huart1         ///< CubeMX UART handle for the UART transport
#endif

/* Modbus port ---------------------------------------------------------------*/

#ifndef MB_PORT_UART_HANDLE
#define MB_PORT_UART_HANDLE           huart2   ///< CubeMX UART handle; pins, IRQ and DMA set in CubeMX
#endif
#ifndef MB_PORT_USE_DMA
#define MB_PORT_USE_DMA               0        ///< 1: DMA reception/transmission, 0: interrupts
#endif
/* RS-485 driver enable pin. Remove both lines for RS-232 or when MB_PORT_HW_DE is used. */
#define MB_PORT_DE_GPIO_PORT          GPIOA
#define MB_PORT_DE_GPIO_PIN           GPIO_PIN_1
/* Hardware driver enable of the USART (DE pin configured in CubeMX): */
/* #define MB_PORT_HW_DE              1 */

/* HAL UART callbacks --------------------------------------------------------*/

/* 1: port_stm32.c defines HAL_UARTEx_RxEventCallback, HAL_UART_TxCpltCallback and
 * HAL_UART_ErrorCallback. 0: the project defines them and calls PortStm32_UartRxEvent,
 * PortStm32_UartTxCplt and PortStm32_UartError from them. */
#ifndef REGMAP_LIB_HAL_UART_CALLBACKS
#define REGMAP_LIB_HAL_UART_CALLBACKS 1
#endif

/* Firmware upgrade port -----------------------------------------------------*/

/* Flash area receiving the new image; linker symbols from configuration.h. The start must be
 * aligned to the erase unit (sector or page). */
#ifndef UPGRADE_PORT_AREA_ADDRESS
#define UPGRADE_PORT_AREA_ADDRESS     ((uint32_t)(uintptr_t)CONF_C_APP_BUFFER_OFFSET)
#endif
#ifndef UPGRADE_PORT_AREA_SIZE
#define UPGRADE_PORT_AREA_SIZE        ((uint32_t)(uintptr_t)CONF_C_APPLICATION_MAX_SIZE)
#endif
/* Image check after the last packet, e.g. System_VerifyImage((uint32_t *)(address)) */
#ifndef UPGRADE_PORT_VERIFY
#define UPGRADE_PORT_VERIFY(address, size)  (STATUS_OK)
#endif

#endif /* REGMAP_LIB_CONF_H_ */
