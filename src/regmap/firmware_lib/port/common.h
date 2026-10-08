/**
 * @file       common.h
 * @brief      Common definitions for all modules
 *
 * Port file: written once by `regmap export-lib`, then owned by the project. If the project
 * already has its own common.h, put its content here (it must keep the definitions below).
 */
#ifndef APPLICATION_COMMON_H_
#define APPLICATION_COMMON_H_

/* Includes ------------------------------------------------------------------*/

#include "main.h" /* CubeMX main.h includes the HAL header of the MCU family */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Definitions----------------------------------------------------------------*/

#define STATUS_OK       0   ///< Success
#define STATUS_ERROR    1   ///< Error or fail
#define STATUS_TIMEOUT  2   ///< Timeout
#define STATUS_BUSY     3   ///< Busy, try again later

/* Macros --------------------------------------------------------------------*/

#ifndef MIN
#define MIN(a, b)   (((a)>(b))?(b):(a))
#endif

#ifndef MAX
#define MAX(a, b)   (((a)<(b))?(b):(a))
#endif

/** Saturate x to the upper bound val */
#define SAT_UP(x, val)      ((x) = ((x)>(val))?(val):(x))

/** Saturate x to the lower bound val */
#define SAT_DOWN(x, val)    ((x) = ((x)<(val))?(val):(x))

#ifndef UNUSED
#define UNUSED(x)   ((void)(x))
#endif

#ifndef __weak
#define __weak      __attribute__((weak))
#endif

#ifndef __packed
#define __packed    __attribute__((packed))
#endif

#ifndef __aligned
#define __aligned(x) __attribute__((aligned(x)))
#endif

/** True when the HAL tick has passed the time stamp a */
#define TICK_EXPIRED(a) (HAL_GetTick() - (a) < 0x7fffffff)

#define GET_BYTE_0(a)       ((uint8_t) ((a) & 0xff))
#define GET_BYTE_1(a)       ((uint8_t) (((a) >> 8) & 0xff))
#define GET_BYTE_2(a)       ((uint8_t) (((a) >> 16) & 0xff))
#define GET_BYTE_3(a)       ((uint8_t) (((a) >> 24) & 0xff))

/** printf-like debug output, disabled by default */
#define PRINTF(...)

#define ASSERT_PARAM(expr)

#define ERR_PRINT(ret, code)  PRINTF("ERR: code [%d] line [%d] file [%s] \n\r", code, __LINE__, __FILE__)

#define CATCH_ERROR(retValue, errorCode)  \
  do {                                    \
    if ((retValue) != 0)                  \
    {                                     \
      ERR_PRINT(retValue, errorCode);     \
    }                                     \
  } while (0)

/* Typedefs-------------------------------------------------------------------*/

/** General pointer to function type */
typedef void (*System_Callback_t)(void);

/** General status return type */
typedef int16_t Status_t;

#endif /* APPLICATION_COMMON_H_ */
