/**
 * @file       lebin_port_usb_cdc.c
 * @brief      LeBin port for the ST USB Device Library (CDC class)
 *
 * Port file: written once by `regmap export-lib`, then owned by the project.
 * In the CubeMX usbd_cdc_if.c, include "port_stm32.h" and make CDC_Receive_FS (or _HS) call
 * LebinPortUsb_Receive(Buf, *Len) and return USBD_OK (see doc/firmware-lib.md).
 */

#include "port.h"
#include "port_stm32.h"

#if REGMAP_LIB_LEBIN && (LEBIN_PORT_TRANSPORT == LEBIN_PORT_USB_CDC)

#include "lebin.h"
#include "usbd_cdc.h"

extern USBD_HandleTypeDef LEBIN_PORT_USB_DEVICE;

static USBD_CDC_HandleTypeDef *LebinPortUsb_Cdc(void)
{
  return (USBD_CDC_HandleTypeDef *)LEBIN_PORT_USB_DEVICE.pClassData;
}


Status_t LebinPort_Init(void)
{
  return STATUS_OK;
}


bool LebinPort_TxReady(void)
{
  USBD_CDC_HandleTypeDef *cdc = LebinPortUsb_Cdc();

  /* Not connected: responses are dropped by LebinPort_Send */
  return (cdc == NULL) || (cdc->TxState == 0u);
}


Status_t LebinPort_Send(const uint8_t *data, uint16_t len)
{
  USBD_CDC_HandleTypeDef *cdc = LebinPortUsb_Cdc();

  if (cdc == NULL)
  {
    return STATUS_ERROR;
  }
  if (cdc->TxState != 0u)
  {
    return STATUS_BUSY;
  }
  (void)USBD_CDC_SetTxBuffer(&LEBIN_PORT_USB_DEVICE, (uint8_t *)data, len);

  return (USBD_CDC_TransmitPacket(&LEBIN_PORT_USB_DEVICE) == USBD_OK) ? STATUS_OK : STATUS_BUSY;
}


void LebinPortUsb_Receive(uint8_t *buf, uint32_t len)
{
  /* The library copies the data, so the USB buffer can receive the next packet at once */
  Lebin_RxBytes(buf, len);
  (void)USBD_CDC_SetRxBuffer(&LEBIN_PORT_USB_DEVICE, buf);
  (void)USBD_CDC_ReceivePacket(&LEBIN_PORT_USB_DEVICE);
}

#endif
