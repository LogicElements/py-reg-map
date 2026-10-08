/* Minimal STM32 HAL/CMSIS stand-in for syntax checks of the ports. Signatures follow the
 * STM32Cube HAL; only what the ports use is declared. */
#ifndef MAIN_H_
#define MAIN_H_

#include <stdint.h>

#define __packed            __attribute__((packed))
#define __aligned(x)        __attribute__((aligned(x)))
#define __IO                volatile

typedef enum { HAL_OK = 0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;

uint32_t HAL_GetTick(void);
uint32_t __get_PRIMASK(void);
void __set_PRIMASK(uint32_t primask);
void __disable_irq(void);
uint32_t __REV16(uint32_t value);
void SCB_CleanDCache_by_Addr(uint32_t *addr, int32_t dsize);
void SCB_InvalidateDCache_by_Addr(void *addr, int32_t dsize);

/* GPIO */
typedef struct { uint32_t ODR; } GPIO_TypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;
#define GPIOA       ((GPIO_TypeDef *)0x48000000u)
#define GPIO_PIN_1  ((uint16_t)0x0002)
void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState);

/* DMA */
typedef struct { uint32_t Instance; } DMA_HandleTypeDef;
#define DMA_IT_HT   0x00000008u
#define __HAL_DMA_DISABLE_IT(handle, it)  ((void)(handle), (void)(it))

/* UART */
typedef struct
{
  uint32_t BaudRate;
  uint32_t WordLength;
  uint32_t StopBits;
  uint32_t Parity;
} UART_InitTypeDef;

typedef uint32_t HAL_UART_StateTypeDef;
#define HAL_UART_STATE_READY  0x20u

typedef struct
{
  UART_InitTypeDef Init;
  __IO HAL_UART_StateTypeDef gState;
  DMA_HandleTypeDef *hdmarx;
} UART_HandleTypeDef;

#define UART_WORDLENGTH_8B     0x00000000u
#define UART_WORDLENGTH_9B     0x00001000u
#define UART_STOPBITS_1        0x00000000u
#define UART_STOPBITS_2        0x00002000u
#define UART_PARITY_NONE       0x00000000u
#define UART_PARITY_EVEN       0x00000400u
#define UART_PARITY_ODD        0x00000600u
#define UART_DE_POLARITY_HIGH  0x00000000u

HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *huart);
HAL_StatusTypeDef HAL_RS485Ex_Init(UART_HandleTypeDef *huart, uint32_t Polarity,
                                   uint32_t AssertionTime, uint32_t DeassertionTime);
HAL_StatusTypeDef HAL_UART_Abort(UART_HandleTypeDef *huart);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *huart);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *huart, const uint8_t *pData,
                                       uint16_t Size);
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *huart, const uint8_t *pData,
                                        uint16_t Size);
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_IT(UART_HandleTypeDef *huart, uint8_t *pData,
                                              uint16_t Size);
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *huart, uint8_t *pData,
                                               uint16_t Size);
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart);

/* FLASH */
#define FLASH_BASE  0x08000000u

typedef struct
{
  uint32_t TypeErase;
  uint32_t Banks;
#ifdef STUB_FAMILY_PAGE
  uint32_t Page;
  uint32_t NbPages;
#else
  uint32_t Sector;
  uint32_t NbSectors;
  uint32_t VoltageRange;
#endif
} FLASH_EraseInitTypeDef;

#ifdef STUB_FAMILY_PAGE
/* Page-based family (G4-like, dual bank) */
#define FLASH_TYPEERASE_PAGES        0x00u
#define FLASH_TYPEPROGRAM_DOUBLEWORD 0x00u
#define FLASH_PAGE_SIZE              0x800u
#define FLASH_BANK_SIZE              0x40000u
#define FLASH_BANK_1                 0x01u
#define FLASH_BANK_2                 0x02u
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t TypeProgram, uint32_t Address, uint64_t Data);
#else
/* STM32H7 (dual bank) */
#define DUAL_BANK
#define FLASH_TYPEERASE_SECTORS          0x00u
#define FLASH_TYPEPROGRAM_FLASHWORD      0x01u
#define FLASH_NB_32BITWORD_IN_FLASHWORD  8u
#define FLASH_SECTOR_SIZE                0x00020000u
#define FLASH_BANK1_BASE                 0x08000000u
#define FLASH_BANK2_BASE                 0x08100000u
#define FLASH_BANK_1                     0x01u
#define FLASH_BANK_2                     0x02u
#define FLASH_VOLTAGE_RANGE_3            0x20u
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t TypeProgram, uint32_t FlashAddress,
                                    uint32_t DataAddress);
#endif

HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *pEraseInit, uint32_t *SectorError);

#endif /* MAIN_H_ */
