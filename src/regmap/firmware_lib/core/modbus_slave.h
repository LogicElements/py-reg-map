/**
 * @file       modbus_slave.h
 * @brief      Modbus RTU slave
 * @regmap-lib
 *
 * @defgroup grMbSlave Modbus RTU slave
 * @{
 * @brief Modbus RTU slave on top of the generated mb_rtu_app callbacks
 *
 * @par Main features:
 * - Function codes 3, 4, 6 and 16; others answered with exception 01
 * - Frames delimited by the port (UART idle line); a frame failing the CRC waits up to t3.5
 *   for the rest
 * - Communication timeout reported through MbSlave_TimeoutChanged
 * - Settings changed while running are applied after the current response
 *
 * @par Example
 * @code
 * MbSlave_Config_t cfg;
 * ConfigApp_ModbusConfig(&cfg);
 * MbSlave_Init(&cfg);
 * while (1) { MbSlave_Handle(); }
 * @endcode
 */
#ifndef MODBUS_SLAVE_H_
#define MODBUS_SLAVE_H_

/* Includes ------------------------------------------------------------------*/

#include "common.h"
#include "regmap_lib_conf.h"

/* Definitions----------------------------------------------------------------*/

/** Largest RTU frame: address, PDU (253 bytes) and CRC */
#define MBSLAVE_FRAME_MAX       256u

/* Typedefs-------------------------------------------------------------------*/

typedef enum
{
  MBSLAVE_PARITY_NONE = 0,
  MBSLAVE_PARITY_EVEN = 1,
  MBSLAVE_PARITY_ODD = 2,
} MbSlave_Parity_t;

typedef struct
{
  uint8_t address;      ///< Slave address, saturated to 1..247
  uint32_t baudRate;    ///< Bit/s
  uint8_t parity;       ///< MbSlave_Parity_t
  uint8_t stopBits;     ///< 1 or 2
  uint32_t timeoutMs;   ///< Communication timeout, 0 = disabled
} MbSlave_Config_t;

/* Functions -----------------------------------------------------------------*/

/** Initialise the slave and the port with the given settings */
Status_t MbSlave_Init(const MbSlave_Config_t *cfg);

/** Request new settings; applied by MbSlave_Handle when no response is being sent */
Status_t MbSlave_Reconfigure(const MbSlave_Config_t *cfg);

/** Process a received frame, send the response, handle timeout; call periodically */
Status_t MbSlave_Handle(void);

/** True while no valid frame arrived for the configured timeout */
bool MbSlave_IsTimeout(void);

/** Number of dropped frames and chunks since MbSlave_Init */
uint32_t MbSlave_GetErrorCount(void);

/** Modbus CRC16 of data; the low byte is transmitted first */
uint16_t MbSlave_Crc16(const uint8_t *data, uint16_t len);

/** ISR (port): a chunk of a frame was received */
void MbSlave_RxFrame(const uint8_t *data, uint16_t len);

/** ISR (port): the response has been transmitted completely */
void MbSlave_TxDone(void);

/** Application hook (config_app.c): the communication timeout state changed */
void MbSlave_TimeoutChanged(bool timeout);

#endif /* MODBUS_SLAVE_H_ */
/** @} */
