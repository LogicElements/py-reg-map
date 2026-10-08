/**
 * @file       modbus_port_stm32.c
 * @brief      Modbus RTU slave port for the STM32 HAL UART
 *
 * Port file: written once by `regmap export-lib`, then owned by the project.
 * The UART (pins, clock, NVIC, optional DMA) is configured in CubeMX; this file only changes
 * baud rate, parity and stop bits and handles reception and RS-485 direction.
 */

#include "port.h"
#include "port_stm32.h"

#if REGMAP_LIB_MODBUS

extern UART_HandleTypeDef MB_PORT_UART_HANDLE;

#ifndef MB_PORT_DMA_BUFFER_ATTR
#define MB_PORT_DMA_BUFFER_ATTR   /* e.g. __attribute__((section(".dma_buffer"))) on STM32H7 */
#endif

/* With DMA and a data cache (Cortex-M7) the buffers are cleaned/invalidated around transfers,
 * so they are aligned to the 32-byte cache line and padded to whole lines. */
#define MB_PORT_BUFFER_SIZE  ((MBSLAVE_FRAME_MAX + 31u) & ~31u)

/** Reception buffer of one idle-line chunk */
static uint8_t mbRxBuffer[MB_PORT_BUFFER_SIZE] MB_PORT_DMA_BUFFER_ATTR __attribute__((aligned(32)));

#if MB_PORT_USE_DMA
/** Transmission buffer: the core buffer may lie in memory the DMA cannot reach (H7 DTCM) */
static uint8_t mbTxBuffer[MB_PORT_BUFFER_SIZE] MB_PORT_DMA_BUFFER_ATTR __attribute__((aligned(32)));
#endif

static void MbPortStm32_DirReceive(void)
{
#if defined(MB_PORT_DE_GPIO_PORT) && !defined(MB_PORT_HW_DE)
  HAL_GPIO_WritePin(MB_PORT_DE_GPIO_PORT, MB_PORT_DE_GPIO_PIN, GPIO_PIN_RESET);
#endif
}


static void MbPortStm32_DirTransmit(void)
{
#if defined(MB_PORT_DE_GPIO_PORT) && !defined(MB_PORT_HW_DE)
  HAL_GPIO_WritePin(MB_PORT_DE_GPIO_PORT, MB_PORT_DE_GPIO_PIN, GPIO_PIN_SET);
#endif
}


static void MbPortStm32_StartReceive(void)
{
  MbPortStm32_DirReceive();
#if MB_PORT_USE_DMA
  if (HAL_UARTEx_ReceiveToIdle_DMA(&MB_PORT_UART_HANDLE, mbRxBuffer, MBSLAVE_FRAME_MAX) == HAL_OK)
  {
    /* Only idle-line and buffer-full events are wanted */
    __HAL_DMA_DISABLE_IT(MB_PORT_UART_HANDLE.hdmarx, DMA_IT_HT);
  }
#else
  (void)HAL_UARTEx_ReceiveToIdle_IT(&MB_PORT_UART_HANDLE, mbRxBuffer, MBSLAVE_FRAME_MAX);
#endif
}


Status_t MbPort_Init(const MbSlave_Config_t *cfg)
{
  UART_HandleTypeDef *huart = &MB_PORT_UART_HANDLE;
  HAL_StatusTypeDef status;

  (void)HAL_UART_Abort(huart);

  huart->Init.BaudRate = cfg->baudRate;
  huart->Init.StopBits = (cfg->stopBits == 2u) ? UART_STOPBITS_2 : UART_STOPBITS_1;
  switch (cfg->parity)
  {
    case MBSLAVE_PARITY_EVEN:
      huart->Init.Parity = UART_PARITY_EVEN;
      huart->Init.WordLength = UART_WORDLENGTH_9B; /* 8 data bits + parity */
      break;
    case MBSLAVE_PARITY_ODD:
      huart->Init.Parity = UART_PARITY_ODD;
      huart->Init.WordLength = UART_WORDLENGTH_9B;
      break;
    default:
      huart->Init.Parity = UART_PARITY_NONE;
      huart->Init.WordLength = UART_WORDLENGTH_8B;
      break;
  }

#if defined(MB_PORT_HW_DE)
  status = HAL_RS485Ex_Init(huart, UART_DE_POLARITY_HIGH, 0, 0);
#else
  status = HAL_UART_Init(huart);
#endif
  if (status != HAL_OK)
  {
    return STATUS_ERROR;
  }

  MbPortStm32_StartReceive();
  return STATUS_OK;
}


Status_t MbPort_Send(const uint8_t *data, uint16_t len)
{
  HAL_StatusTypeDef status;

  (void)HAL_UART_AbortReceive(&MB_PORT_UART_HANDLE);
  MbPortStm32_DirTransmit();
#if MB_PORT_USE_DMA
  memcpy(mbTxBuffer, data, len);
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  SCB_CleanDCache_by_Addr((uint32_t *)mbTxBuffer, (int32_t)sizeof(mbTxBuffer));
#endif
  status = HAL_UART_Transmit_DMA(&MB_PORT_UART_HANDLE, mbTxBuffer, len);
#else
  status = HAL_UART_Transmit_IT(&MB_PORT_UART_HANDLE, (uint8_t *)data, len);
#endif
  if (status != HAL_OK)
  {
    MbPortStm32_StartReceive();
    return STATUS_ERROR;
  }

  return STATUS_OK;
}


void MbPortStm32_RxEvent(UART_HandleTypeDef *huart, uint16_t size)
{
  if (huart != &MB_PORT_UART_HANDLE)
  {
    return;
  }
  if (size > 0u)
  {
#if MB_PORT_USE_DMA && defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    SCB_InvalidateDCache_by_Addr((uint32_t *)mbRxBuffer, (int32_t)sizeof(mbRxBuffer));
#endif
    MbSlave_RxFrame(mbRxBuffer, size);
  }
  MbPortStm32_StartReceive();
}


void MbPortStm32_TxCplt(UART_HandleTypeDef *huart)
{
  if (huart != &MB_PORT_UART_HANDLE)
  {
    return;
  }
  /* Transmission complete (TC): the last stop bit has left the line */
  MbPortStm32_StartReceive();
  MbSlave_TxDone();
}


void MbPortStm32_Error(UART_HandleTypeDef *huart)
{
  if (huart != &MB_PORT_UART_HANDLE)
  {
    return;
  }
  (void)HAL_UART_AbortReceive(huart);
  MbPortStm32_StartReceive();
}

#endif /* REGMAP_LIB_MODBUS */
