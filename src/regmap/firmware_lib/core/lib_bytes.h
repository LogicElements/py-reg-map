/**
 * @file       lib_bytes.h
 * @brief      Byte order helpers for protocol buffers
 * @regmap-lib
 *
 * Packet fields are read and written byte by byte, so buffers may be unaligned (Cortex-M0/M0+
 * fault on unaligned word access).
 */
#ifndef LIB_BYTES_H_
#define LIB_BYTES_H_

#include <stdint.h>

static inline uint16_t Bytes_GetU16Le(const uint8_t *p)
{
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline uint16_t Bytes_GetU16Be(const uint8_t *p)
{
  return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static inline uint32_t Bytes_GetU32Le(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void Bytes_PutU16Le(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static inline void Bytes_PutU16Be(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)v;
}

static inline void Bytes_PutU32Le(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

#endif /* LIB_BYTES_H_ */
