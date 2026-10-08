/**
 * @file       port.h
 * @brief      Interface between the regmap library core and the MCU port
 * @regmap-lib
 *
 * The core calls these functions; the port (port_stm32.c, modbus_port_stm32.c, ...) defines
 * them. "ISR" marks functions that may be called from interrupt context.
 */
#ifndef PORT_H_
#define PORT_H_

#include "common.h"
#include "regmap_lib_conf.h"

/* General -------------------------------------------------------------------*/

/** Millisecond tick, wraps around at 2^32 */
uint32_t Port_GetTickMs(void);

/** Enter a short critical section (interrupts off); calls may nest */
void Port_CriticalEnter(void);

/** Leave the critical section entered by Port_CriticalEnter */
void Port_CriticalExit(void);

/* Modbus RTU slave ----------------------------------------------------------*/

#if REGMAP_LIB_MODBUS
#include "modbus_slave.h"

/** (Re)configure the UART and start reception (ends any running transfer) */
Status_t MbPort_Init(const MbSlave_Config_t *cfg);

/** Send a response; MbSlave_TxDone must be called when the last bit left the line */
Status_t MbPort_Send(const uint8_t *data, uint16_t len);

/** ISR: a chunk was received; wake the task that calls MbSlave_Handle (may be empty) */
void MbPort_NotifyFromIsr(void);
#endif

/* LeBin ---------------------------------------------------------------------*/

#if REGMAP_LIB_LEBIN
/** Start reception (UART transport); nothing to do for USB CDC */
Status_t LebinPort_Init(void);

/** True when the previous response has been handed over and the buffer may be reused */
bool LebinPort_TxReady(void);

/** Send a response; data stays valid until LebinPort_TxReady returns true again */
Status_t LebinPort_Send(const uint8_t *data, uint16_t len);

/** ISR: bytes were received; wake the task that calls Lebin_Handle (may be empty) */
void LebinPort_NotifyFromIsr(void);
#endif

/* Firmware upgrade ----------------------------------------------------------*/

#if REGMAP_LIB_UPGRADE
/** Start a new image; size 0 = unknown (erase on the fly), otherwise erase the whole size */
Status_t UpgradePort_Begin(uint32_t size);

/** Program len bytes at offset of the upgrade area; erases units the write enters. offset and
 * len are multiples of UPGRADE_WRITE_UNIT (32); data may be unaligned. */
Status_t UpgradePort_Write(uint32_t offset, const uint8_t *data, uint32_t len);

/** Check the received image of the given size */
Status_t UpgradePort_Verify(uint32_t size);

/** Apply the new image (e.g. request a restart into the bootloader) */
void UpgradePort_Apply(void);
#endif

#endif /* PORT_H_ */
