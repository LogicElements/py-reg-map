/**
 * @file       mb_upgrade.h
 * @brief      Firmware upgrade over Modbus RTU holding registers
 * @regmap-lib
 *
 * @defgroup grMbUpgrade Modbus upgrade
 * @{
 * @brief Firmware received in pages through holding registers from MB_UPGR_BASE_ADDRESS
 *
 * The master writes the header (type, mode, size), then for every page the page size, offset
 * and data followed by write-done = 1. The slave programs the page in MbUpgr_Handle and sets
 * the status to ready (or done after the last page). Called from modbus_slave.c.
 */
#ifndef MB_UPGRADE_H_
#define MB_UPGRADE_H_

#include "common.h"
#include "regmap_lib_conf.h"

/** Base address of upgrade registers */
#define MB_UPGR_BASE_ADDRESS    1000

/** Size of page in bytes */
#define MB_UPGR_PAGE_SIZE       64

/** Last address of upgrade registers */
#define MB_UPGR_END_ADDRESS     (MB_UPGR_BASE_ADDRESS + MB_UPGR_PAGE_SIZE / 2 + 9)

typedef enum
{
  MB_UPGR_MODE_ERASE_ON_FLY = 0,
  MB_UPGR_MODE_ERASE_AT_START = 1,
  MB_UPGR_MODE_APPLY = 2,
} MbUpgr_Mode_t;

typedef enum
{
  MB_UPGR_STATUS_BUSY = 0,
  MB_UPGR_STATUS_READY = 1,
  MB_UPGR_STATUS_DONE_OK = 2,
  MB_UPGR_STATUS_DONE_ERROR = 3,
} MbUpgr_Status_t;

/** Reset registers, status busy */
Status_t MbUpgr_Init(void);

/** Start the session after a header write, program a page after write-done; call when idle */
Status_t MbUpgr_Handle(void);

/** Write `count` registers from `address` (wire data, big-endian words) */
Status_t MbUpgr_WriteRegisters(uint16_t address, uint16_t count, const uint8_t *data);

/** Read `count` registers from `address` into wire data */
Status_t MbUpgr_ReadRegisters(uint16_t address, uint16_t count, uint8_t *data);

/** Value of the type register */
uint16_t MbUpgr_GetType(void);

/** Value of the mode register */
uint16_t MbUpgr_GetMode(void);

#endif /* MB_UPGRADE_H_ */
/** @} */
