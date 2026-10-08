/* Fake port for the host tests: simulated tick, captured transmissions, RAM flash. */
#ifndef FAKE_PORT_H_
#define FAKE_PORT_H_

#include "port.h"

/* General */
extern uint32_t fake_tick;

/* Modbus */
#include "modbus_slave.h"

extern MbSlave_Config_t fake_mb_cfg;      /* last configuration given to MbPort_Init */
extern int fake_mb_init_count;
extern uint8_t fake_mb_tx[MBSLAVE_FRAME_MAX];
extern uint16_t fake_mb_tx_len;
extern int fake_mb_tx_count;

void fake_mb_reset(void);

/* LeBin */
#include "lebin.h"

extern bool fake_lebin_busy;              /* LebinPort_TxReady returns !fake_lebin_busy */
extern uint8_t fake_lebin_tx[LEBIN_PACKET_MAX];
extern uint16_t fake_lebin_tx_len;
extern int fake_lebin_tx_count;
extern int fake_lebin_init_count;

void fake_lebin_reset(void);

/* Upgrade */
#define FAKE_FLASH_SIZE 4096u

extern uint8_t fake_flash[FAKE_FLASH_SIZE];
extern int fake_upgrade_begin_count;
extern uint32_t fake_upgrade_begin_size;
extern int fake_verify_count;
extern uint32_t fake_verify_size;
extern Status_t fake_verify_result;
extern int fake_apply_count;

void fake_upgrade_reset(void);

#endif /* FAKE_PORT_H_ */
