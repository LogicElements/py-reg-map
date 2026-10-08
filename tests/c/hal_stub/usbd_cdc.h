/* Minimal ST USB Device Library CDC stand-in for syntax checks of the ports. */
#ifndef USBD_CDC_H_
#define USBD_CDC_H_

#include <stdint.h>

#define USBD_OK 0u

typedef struct
{
  void *pClassData;
} USBD_HandleTypeDef;

typedef struct
{
  volatile uint32_t TxState;
} USBD_CDC_HandleTypeDef;

uint8_t USBD_CDC_SetTxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff, uint32_t length);
uint8_t USBD_CDC_SetRxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff);
uint8_t USBD_CDC_TransmitPacket(USBD_HandleTypeDef *pdev);
uint8_t USBD_CDC_ReceivePacket(USBD_HandleTypeDef *pdev);

#endif /* USBD_CDC_H_ */
