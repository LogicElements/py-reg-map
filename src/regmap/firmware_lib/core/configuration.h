/**
 * @file       configuration.h
 * @brief      Configuration and tools for register map access
 * @regmap-lib
 *
 * @defgroup grConfig Configuration
 * @{
 * @brief Tools for accessing configuration and register map
 *
 * @par Main features:
 * - Access macros to the registers according to their type
 * - Flash storage streams
 * - Callback for register change (Config_Callback in config_app.c)
 * - Synchronisation stream
 */
#ifndef CONFIGURATION_H_
#define CONFIGURATION_H_

/* Includes ------------------------------------------------------------------*/

#include "common.h"
#include "reg_map.h"

/* Macros --------------------------------------------------------------------*/

/** Block number of the register */
#define CONF_BLOCK_ID(id) (((id) & 0xFF000000) >> 24)

/** Address of the register within the block */
#define CONF_ADDR_ID(id)  (((id) & 0x00FFF000) >> 12)

/** Byte length of the register */
#define CONF_BYTE_LEN_ID(id)  (CONF_LENGTH[((id) & 0x0000000F)])

/** Variable type of the register */
#define CONF_TYPE_ID(id)    (((id) & 0x0F00) >> 8)

/** uint8_t pointer to the beginning of the register */
#define CONF_PTR(id) (CONF_REG[CONF_BLOCK_ID(id)] + CONF_ADDR_ID(id))

/** Get/set uint32_t value of the register */
#define CONF_INT(id) (*((uint32_t *)(CONF_REG[CONF_BLOCK_ID(id)] + CONF_ADDR_ID(id))))

/** Get/set uint16_t value of the register */
#define CONF_SHORT(id) (*((uint16_t *)(CONF_REG[CONF_BLOCK_ID(id)] + CONF_ADDR_ID(id))))

/** Get/set uint8_t value of the register */
#define CONF_BYTE(id) (*((uint8_t *)(CONF_REG[CONF_BLOCK_ID(id)] + CONF_ADDR_ID(id))))

/** Get/set float value of the register */
#define CONF_FLOAT(id) (*((float *)(CONF_REG[CONF_BLOCK_ID(id)] + CONF_ADDR_ID(id))))

/* Circuit-dependent macros (blocks repeated per circuit) */

#define CONF_PTR_C(id, c) (CONF_REG[CONF_BLOCK_ID(id) + (c)] + CONF_ADDR_ID(id))
#define CONF_INT_C(id, c) (*((uint32_t *)(CONF_REG[CONF_BLOCK_ID(id) + (c)] + CONF_ADDR_ID(id))))
#define CONF_SHORT_C(id, c) (*((uint16_t *)(CONF_REG[CONF_BLOCK_ID(id) + (c)] + CONF_ADDR_ID(id))))
#define CONF_BYTE_C(id, c) (*((uint8_t *)(CONF_REG[CONF_BLOCK_ID(id) + (c)] + CONF_ADDR_ID(id))))
#define CONF_FLOAT_C(id, c) (*((float *)(CONF_REG[CONF_BLOCK_ID(id) + (c)] + CONF_ADDR_ID(id))))
#define CONF_ID_C(id, c)  ((id) + ((c) * 0x01000000))

/* Array access macros */

#define CONF_FLOAT_A(id, a) (*((float *)(CONF_REG[CONF_BLOCK_ID(id)] + CONF_ADDR_ID(id) + (a) * sizeof(float))))
#define CONF_INT_A(id, a) (*((uint32_t *)(CONF_REG[CONF_BLOCK_ID(id)] + CONF_ADDR_ID(id) + (a) * sizeof(uint32_t))))
#define CONF_SHORT_A(id, a) (*((uint16_t *)(CONF_REG[CONF_BLOCK_ID(id)] + CONF_ADDR_ID(id) + (a) * sizeof(uint16_t))))
#define CONF_BYTE_A(id, a) (*((uint8_t *)(CONF_REG[CONF_BLOCK_ID(id)] + CONF_ADDR_ID(id) + (a) * sizeof(uint8_t))))

/* Constants -----------------------------------------------------------------*/

/* Defined in reg_map.c */
extern conf_reg_t conf;
extern uint8_t* const CONF_REG[CONF_REG_BLOCK_NUMBER];
extern const uint32_t CONF_REG_LIMIT[CONF_REG_BLOCK_NUMBER];
extern const uint32_t CONF_REG_FLASH[CONF_REG_FLASH_NUMBER];
extern const uint32_t CONF_REG_LOGGER[CONF_REG_LOGGER_NUMBER];
extern const uint32_t CONF_REG_CALIB[CONF_REG_CALIB_NUMBER];
extern const uint32_t CONF_REG_SYNCED[CONF_REG_SYNCED_NUMBER];

/* Defined in configuration.c */
extern const uint32_t CONF_FIRMWARE_INFO[8];
extern const uint32_t CONF_FIRMWARE_INFO_DEFAULT[8];
extern const uint32_t CONF_LENGTH[16];

/* Defined by the linker script. A project without such a symbol defines the name as a macro
 * (e.g. in main.h) instead. */
#ifndef CONF_FW_INFO_OFFSET
extern const uint8_t CONF_FW_INFO_OFFSET[] __asm__("_LD_FW_INFO_OFFSET");
#endif
#ifndef CONF_C_BOOTLOADER_OFFSET
extern const uint8_t CONF_C_BOOTLOADER_OFFSET[] __asm__("_LD_ADDRESS_BOOTLOADER");
#endif
#ifndef CONF_C_APPLICATION_OFFSET
extern const uint8_t CONF_C_APPLICATION_OFFSET[] __asm__("_LD_ADDRESS_APPLICATION");
#endif
#ifndef CONF_C_CALIBRATION_OFFSET
extern const uint8_t CONF_C_CALIBRATION_OFFSET[] __asm__("_LD_ADDRESS_CALIBRATION");
#endif
#ifndef CONF_C_APP_BUFFER_OFFSET
extern const uint8_t CONF_C_APP_BUFFER_OFFSET[] __asm__("_LD_ADDRESS_BUFFER_APP");
#endif
#ifndef CONF_C_APPLICATION_MAX_SIZE
extern const uint8_t CONF_C_APPLICATION_MAX_SIZE[] __asm__("_LD_SIZE_BUFFER_APP");
#endif

/* Functions -----------------------------------------------------------------*/

/**
 * Check the register map layout, set factory values and call Config_AppInit.
 * @return STATUS_ERROR when reg_map.h does not match the compiler's structure layout
 */
Status_t Config_Init(void);

/**
 * Check that the register ID lies within the register map.
 * @param id Register ID
 * @return STATUS_OK if the ID is within limits
 */
Status_t Config_CheckLimits(uint32_t id);

/**
 * Notify about a changed register value; calls Config_Callback (config_app.c). Registers with
 * the flash flag ((id & 0x070) == 0x070) are to be stored by the application there.
 * @param id ID of the modified register
 * @return Status of Config_Callback
 */
Status_t Config_ApplyConfig(uint32_t id);

/**
 * Copy flash registers from a stored stream (ID, value pairs) into the register map. The
 * first entry must be CONF_SYS_REGMAP_VERSION with the same major part as the factory value;
 * unknown IDs are skipped.
 * @param data Stream
 * @param length Length of the stream in bytes
 * @return STATUS_ERROR for a different map version or a truncated stream
 */
Status_t Config_ReadStream(const uint8_t *data, uint32_t length);

/**
 * Create a stream of all flash registers for storage.
 * @param data Buffer for the stream
 * @param length Resulting length of the stream
 * @param maxLength Size of the buffer
 * @return STATUS_ERROR if the buffer is too small (the stream holds what fitted)
 */
Status_t Config_FillStream(uint8_t *data, uint32_t *length, uint32_t maxLength);

/**
 * Create a stream of synchronised registers that changed since the last call.
 * @param data Buffer for the stream
 * @param length Resulting length of the stream
 * @return STATUS_OK if at least one register changed
 */
Status_t Config_NeedToSync(uint8_t *data, uint16_t *length);

#endif /* CONFIGURATION_H_ */
/** @} */
