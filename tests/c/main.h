/* Host stand-in for the CubeMX main.h: only what common.h and the generated sources use. */
#ifndef MAIN_H_
#define MAIN_H_

#include <stdint.h>

#define __packed            __attribute__((packed))
#define __aligned(x)        __attribute__((aligned(x)))
#define CONF_SECTION(name)  /* no linker sections on the host */

static inline uint32_t __REV16(uint32_t v)
{
  return ((v & 0x00FF00FFu) << 8) | ((v & 0xFF00FF00u) >> 8);
}

uint32_t HAL_GetTick(void);

/* Linker symbols of configuration.h: firmware info blocks read by the generated reg_map.c */
extern const uint8_t fake_bootloader_image[64];
extern const uint8_t fake_application_image[64];
#define CONF_FW_INFO_OFFSET         ((const uint8_t *)0)
#define CONF_C_BOOTLOADER_OFFSET    fake_bootloader_image
#define CONF_C_APPLICATION_OFFSET   fake_application_image

#endif /* MAIN_H_ */
