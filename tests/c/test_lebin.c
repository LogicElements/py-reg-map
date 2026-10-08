#include "check.h"
#include "configuration.h"
#include "fake_port.h"
#include "lebin.h"
#include "lib_bytes.h"

#define INVALID_ID  0x7F000012u   /* block beyond the map, length code 2 (4 bytes) */

static void lb_setup(void)
{
  fake_tick = 1000;
  fake_lebin_reset();
  Config_Init();
  Lebin_Init();
}

/* Build a packet in buf: header + payload; returns its length. */
static uint16_t lb_packet(uint8_t *buf, uint8_t id, const uint8_t *payload, uint16_t len)
{
  buf[0] = LEBIN_START_BYTE;
  buf[1] = id;
  Bytes_PutU16Le(buf + 2, (uint16_t)(len + LEBIN_HEADER_LEN));
  memcpy(buf + LEBIN_HEADER_LEN, payload, len);
  return (uint16_t)(len + LEBIN_HEADER_LEN);
}

/* Read request for SYS_UPTIME and FACT_SERIAL_NUMBER (20-byte response). */
static uint16_t lb_read_request(uint8_t *buf)
{
  uint8_t payload[8];

  Bytes_PutU32Le(payload, CONF_SYS_UPTIME);
  Bytes_PutU32Le(payload + 4, CONF_FACT_SERIAL_NUMBER);
  return lb_packet(buf, LEBIN_READ_REG, payload, sizeof(payload));
}

static void test_init_starts_port(void)
{
  lb_setup();
  CHECK(fake_lebin_init_count == 1);
}

static void test_read_registers(void)
{
  uint8_t pkt[32];
  uint16_t n;

  lb_setup();
  conf.sys.uptime = 0x11223344;
  conf.fact.serial_number = 0xAABBCCDD;
  n = lb_read_request(pkt);
  Lebin_RxBytes(pkt, n);
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 1);
  CHECK_BYTES(fake_lebin_tx, fake_lebin_tx_len,
              0x90, 0x05, 0x14, 0x00,
              0x12, 0x01, 0x00, 0x00, 0x44, 0x33, 0x22, 0x11,
              0x12, 0x01, 0x00, 0x01, 0xDD, 0xCC, 0xBB, 0xAA);
}

static void test_read_skips_invalid_id(void)
{
  uint8_t payload[8];
  uint8_t pkt[32];
  uint16_t n;

  lb_setup();
  conf.sys.uptime = 0x11223344;
  Bytes_PutU32Le(payload, INVALID_ID);
  Bytes_PutU32Le(payload + 4, CONF_SYS_UPTIME);
  n = lb_packet(pkt, LEBIN_READ_REG, payload, sizeof(payload));
  Lebin_RxBytes(pkt, n);
  Lebin_Handle();
  CHECK_BYTES(fake_lebin_tx, fake_lebin_tx_len,
              0x90, 0x05, 0x0C, 0x00, 0x12, 0x01, 0x00, 0x00, 0x44, 0x33, 0x22, 0x11);
}

static void test_write_register(void)
{
  uint8_t payload[6];
  uint8_t pkt[32];
  uint16_t n;

  lb_setup();
  Bytes_PutU32Le(payload, CONF_COM_MB_TIMEOUT);
  Bytes_PutU16Le(payload + 4, 10);
  n = lb_packet(pkt, LEBIN_WRITE_REG, payload, sizeof(payload));
  Lebin_RxBytes(pkt, n);
  Lebin_Handle();
  CHECK(conf.com.mb_timeout == 10);
  CHECK_BYTES(fake_lebin_tx, fake_lebin_tx_len,
              0x90, 0x05, 0x0A, 0x00, 0x71, 0x81, 0x00, 0x03, 0x0A, 0x00);
}

static void test_write_invalid_id_then_valid(void)
{
  uint8_t payload[14];
  uint8_t pkt[32];
  uint16_t n;

  lb_setup();
  Bytes_PutU32Le(payload, INVALID_ID);
  Bytes_PutU32Le(payload + 4, 0x01020304u);
  Bytes_PutU32Le(payload + 8, CONF_COM_MB_TIMEOUT);
  Bytes_PutU16Le(payload + 12, 11);
  n = lb_packet(pkt, LEBIN_WRITE_REG, payload, sizeof(payload));
  Lebin_RxBytes(pkt, n);
  Lebin_Handle();
  CHECK(conf.com.mb_timeout == 11);
  CHECK_BYTES(fake_lebin_tx, fake_lebin_tx_len,
              0x90, 0x05, 0x12, 0x00,
              0x12, 0x00, 0x00, 0x7F, 0xF0, 0xF0, 0xF0, 0xF0,
              0x71, 0x81, 0x00, 0x03, 0x0B, 0x00);
}

static void test_packet_in_chunks(void)
{
  uint8_t pkt[32];
  uint16_t n;

  lb_setup();
  n = lb_read_request(pkt);
  Lebin_RxBytes(pkt, 3);
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 0);
  Lebin_RxBytes(pkt + 3, (uint32_t)(n - 3));
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 1);
  CHECK(fake_lebin_tx_len == 20);
}

static void test_two_packets_in_one_chunk(void)
{
  uint8_t pkt[64];
  uint16_t n;

  lb_setup();
  n = lb_read_request(pkt);
  n = (uint16_t)(n + lb_read_request(pkt + n));
  Lebin_RxBytes(pkt, n);
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 1); /* one response per call */
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 2);
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 2);
}

static void test_waits_while_port_busy(void)
{
  uint8_t pkt[32];
  uint16_t n;

  lb_setup();
  fake_lebin_busy = true;
  n = lb_read_request(pkt);
  Lebin_RxBytes(pkt, n);
  CHECK(Lebin_Handle() == STATUS_BUSY);
  CHECK(fake_lebin_tx_count == 0);
  fake_lebin_busy = false;
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 1);
}

static void test_resync_after_garbage(void)
{
  uint8_t pkt[32] = {0x00, 0x11};
  uint16_t n;
  uint32_t errors;

  lb_setup();
  errors = Lebin_GetErrorCount();
  n = lb_read_request(pkt + 2);
  Lebin_RxBytes(pkt, (uint32_t)(n + 2));
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 1);
  CHECK(Lebin_GetErrorCount() > errors);
}

static void test_resync_after_bad_length(void)
{
  uint8_t pkt[32] = {0x90, 0x82, 0xFF, 0xFF};
  uint16_t n;

  lb_setup();
  n = lb_read_request(pkt + 4);
  Lebin_RxBytes(pkt, (uint32_t)(n + 4));
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 1);
  CHECK(fake_lebin_tx_len == 20);
}

static void test_stale_partial_packet_is_dropped(void)
{
  uint8_t pkt[32];
  uint16_t n;

  lb_setup();
  n = lb_read_request(pkt);
  Lebin_RxBytes(pkt, 6);
  Lebin_Handle();
  fake_tick += LEBIN_NEW_PACKET_MS + 1;
  Lebin_Handle();
  Lebin_RxBytes(pkt, n);
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 1);
  CHECK(fake_lebin_tx_len == 20);
}

static void test_overflow_drops_chunk(void)
{
  static uint8_t big[LEBIN_RX_BUFFER_SIZE + 1];
  uint8_t pkt[32];
  uint16_t n;
  uint32_t errors;

  lb_setup();
  errors = Lebin_GetErrorCount();
  Lebin_RxBytes(big, sizeof(big));
  CHECK(Lebin_GetErrorCount() == errors + 1);
  n = lb_read_request(pkt);
  Lebin_RxBytes(pkt, n);
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 1);
}

static void test_unknown_packet_has_no_response(void)
{
  const uint8_t payload[4] = {1, 2, 3, 4};
  uint8_t pkt[16];
  uint16_t n;

  lb_setup();
  n = lb_packet(pkt, 0x42, payload, sizeof(payload));
  Lebin_RxBytes(pkt, n);
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 0);
}

static void test_fill_series(void)
{
  uint8_t buf[20];

  CHECK(Lebin_FillSeries(buf, CONF_SYS_UPTIME, 1, 2, 3) == STATUS_OK);
  CHECK_BYTES(buf, sizeof(buf),
              0x90, 0x08, 0x20, 0x00,
              0x12, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
              0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00);
}

void test_lebin(void)
{
  test_init_starts_port();
  test_read_registers();
  test_read_skips_invalid_id();
  test_write_register();
  test_write_invalid_id_then_valid();
  test_packet_in_chunks();
  test_two_packets_in_one_chunk();
  test_waits_while_port_busy();
  test_resync_after_garbage();
  test_resync_after_bad_length();
  test_stale_partial_packet_is_dropped();
  test_overflow_drops_chunk();
  test_unknown_packet_has_no_response();
  test_fill_series();
}
