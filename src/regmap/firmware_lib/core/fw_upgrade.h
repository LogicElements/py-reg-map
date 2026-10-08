/**
 * @file       fw_upgrade.h
 * @brief      Firmware upgrade session shared by LeBin and Modbus
 * @regmap-lib
 */
#ifndef FW_UPGRADE_H_
#define FW_UPGRADE_H_

#include "common.h"
#include "regmap_lib_conf.h"

/** Offsets passed to the port are multiples of this; a shorter last chunk is padded with 0xFF */
#define UPGRADE_WRITE_UNIT  32u

/** Start a session; size 0 = unknown */
Status_t Upgrade_Begin(uint32_t size);

/** Write image data at an offset that is a multiple of UPGRADE_WRITE_UNIT; STATUS_ERROR outside a
 * session (a failed write ends the session) */
Status_t Upgrade_Write(uint32_t offset, const uint8_t *data, uint32_t len);

/** End the session and verify the received image */
Status_t Upgrade_Finish(void);

/** Apply the new image (port decides how) */
void Upgrade_Apply(void);

/** True between Upgrade_Begin and Upgrade_Finish */
bool Upgrade_IsActive(void);

#endif /* FW_UPGRADE_H_ */
