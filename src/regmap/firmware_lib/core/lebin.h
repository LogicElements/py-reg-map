/**
 * @file       lebin.h
 * @brief      LeBin binary protocol
 * @regmap-lib
 *
 * @defgroup grLebin LeBin protocol
 * @{
 * @brief Register read/write and firmware upgrade over a byte stream (USB CDC, UART)
 *
 * Packet: start byte 0x90, packet ID, total length (16 bit LE, header included), payload.
 *
 * @par Example
 * @code
 * Lebin_Init();
 * while (1) { Lebin_Handle(); }
 * @endcode
 */
#ifndef LEBIN_H_
#define LEBIN_H_

/* Includes ------------------------------------------------------------------*/

#include "common.h"
#include "regmap_lib_conf.h"

/* Definitions----------------------------------------------------------------*/

#define LEBIN_START_BYTE        0x90u
#define LEBIN_HEADER_LEN        4u
#define LEBIN_MIN_LENGTH        8u

typedef enum
{
  LEBIN_ERROR          = 1,    ///< Program error code
  LEBIN_READ_REG_RESP  = 5,    ///< Response to read/write register
  LEBIN_TIME_SER       = 8,    ///< Time series
  LEBIN_FW_UPG_ACK     = 127,  ///< Acknowledge of FW upgrade packet
  LEBIN_WRITE_REG      = 129,  ///< Write register request
  LEBIN_READ_REG       = 130,  ///< Read register request
  LEBIN_FW_UPGRADE     = 255,  ///< FW upgrade data
} Lebin_PacketId_t;

/* Functions -----------------------------------------------------------------*/

/** Reset the protocol state and start the port */
Status_t Lebin_Init(void);

/**
 * Assemble received bytes into packets and answer one packet per call; call periodically.
 * @return STATUS_BUSY while a complete packet waits for the port
 */
Status_t Lebin_Handle(void);

/** ISR (port): bytes received; a chunk that does not fit is dropped as a whole */
void Lebin_RxBytes(const uint8_t *data, uint32_t len);

/**
 * Process one complete packet (length >= LEBIN_MIN_LENGTH) and build the response.
 * @return Length of the response, 0 = no response
 */
uint16_t Lebin_ProcessPacket(const uint8_t *req, uint8_t *resp);

/**
 * Fill the header of a time series packet; samples follow at offset 20.
 * @return STATUS_ERROR if the packet would be longer than 65535 bytes
 */
Status_t Lebin_FillSeries(uint8_t *buffer, uint32_t id, uint32_t timestamp, uint32_t delta,
                          uint32_t count);

/** Number of dropped chunks and bytes since Lebin_Init */
uint32_t Lebin_GetErrorCount(void);

#endif /* LEBIN_H_ */
/** @} */
