#include "check.h"
#include "config_app.h"
#include "configuration.h"
#include "fake_port.h"
#include "lib_bytes.h"
#include "mb_rtu_app.h"
#include "modbus_slave.h"

#define HI(x) ((uint8_t)((x) >> 8))
#define LO(x) ((uint8_t)(x))

/* Slave 1 at 19200 Bd; timeoutS is the MB_TIMEOUT register value in seconds. */
static void mb_setup(uint16_t timeoutS)
{
  MbSlave_Config_t cfg;

  fake_tick = 1000;
  fake_mb_reset();
  Config_Init();
  conf.com.mb_address = 1;
  conf.com.mb_baud_rate = MB_BAUD_19200;
  conf.com.mb_timeout = timeoutS;
  ConfigApp_ModbusConfig(&cfg);
  MbSlave_Init(&cfg);
}

/* Append the CRC to `frame` (room for 2 more bytes needed); returns the new length. */
static uint16_t mb_add_crc(uint8_t *frame, uint16_t len)
{
  Bytes_PutU16Le(frame + len, MbSlave_Crc16(frame, len));
  return (uint16_t)(len + 2);
}

/* Receive a request in one idle-line chunk and run the slave once. */
static void mb_request(const uint8_t *pdu, uint16_t len)
{
  uint8_t frame[MBSLAVE_FRAME_MAX];

  memcpy(frame, pdu, len);
  MbSlave_RxFrame(frame, mb_add_crc(frame, len));
  MbSlave_Handle();
}

/* The last response has a valid CRC. */
static bool mb_crc_ok(void)
{
  return fake_mb_tx_len >= 4
         && MbSlave_Crc16(fake_mb_tx, (uint16_t)(fake_mb_tx_len - 2))
            == Bytes_GetU16Le(fake_mb_tx + fake_mb_tx_len - 2);
}

/* Finish the transmission of the last response and run the slave once. */
static void mb_tx_done(void)
{
  MbSlave_TxDone();
  MbSlave_Handle();
}

static void test_crc_known_vector(void)
{
  const uint8_t frame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};

  CHECK(MbSlave_Crc16(frame, sizeof(frame)) == 0xCDC5u); /* sent as C5 CD */
}

static void test_read_holding(void)
{
  const uint8_t req[] = {0x01, 0x03, 0x00, MB_HOLD_COM_MB_TIMEOUT, 0x00, 0x01};

  mb_setup(0);
  conf.com.mb_timeout = 0x1234;
  mb_request(req, sizeof(req));
  CHECK(fake_mb_tx_count == 1);
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x03, 0x02, 0x12, 0x34);
  CHECK(mb_crc_ok());
  mb_tx_done();
}

static void test_read_input_two_words(void)
{
  const uint8_t req[] = {0x01, 0x04, 0x00, MB_INPUT_SYS_UPTIME_0, 0x00, 0x02};

  mb_setup(0);
  conf.sys.uptime = 0x11223344;
  mb_request(req, sizeof(req));
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x04, 0x04, 0x33, 0x44, 0x11, 0x22);
  mb_tx_done();
}

static void test_write_single(void)
{
  const uint8_t req[] = {0x01, 0x06, 0x00, MB_HOLD_COM_MB_TIMEOUT, 0x01, 0x02};

  mb_setup(0);
  mb_request(req, sizeof(req));
  CHECK(conf.com.mb_timeout == 0x0102);
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x06, 0x00, MB_HOLD_COM_MB_TIMEOUT, 0x01, 0x02);
  mb_tx_done();
}

static void test_write_multiple(void)
{
  const uint8_t req[] = {0x01, 0x10, 0x00, MB_HOLD_COM_MB_APPLY, 0x00, 0x02, 0x04,
                         0x00, 0x00, 0x00, 0x05};

  mb_setup(0);
  mb_request(req, sizeof(req));
  CHECK(conf.com.mb_apply == 0);
  CHECK(conf.com.mb_timeout == 5);
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x10, 0x00, MB_HOLD_COM_MB_APPLY, 0x00, 0x02);
  mb_tx_done();
}

static void test_exceptions(void)
{
  const uint8_t fc1[] = {0x01, 0x01, 0x00, 0x00, 0x00, 0x01};
  const uint8_t pastEnd[] = {0x01, 0x03, HI(MB_HOLD_LAST + 1), LO(MB_HOLD_LAST + 1), 0x00, 0x01};
  const uint8_t spanEnd[] = {0x01, 0x03, HI(MB_HOLD_LAST), LO(MB_HOLD_LAST), 0x00, 0x02};
  const uint8_t count0[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x00};
  const uint8_t count126[] = {0x01, 0x04, 0x00, 0x00, 0x00, 0x7E};
  const uint8_t badBytes[] = {0x01, 0x10, 0x00, 0x08, 0x00, 0x01, 0x04, 0x00, 0x01, 0x00, 0x02};
  const uint8_t single[] = {0x01, 0x06, HI(MB_HOLD_LAST + 1), LO(MB_HOLD_LAST + 1), 0x00, 0x01};

  mb_setup(0);
  mb_request(fc1, sizeof(fc1));
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x81, 0x01);
  CHECK(mb_crc_ok());
  mb_tx_done();
  mb_request(pastEnd, sizeof(pastEnd));
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x83, 0x02);
  mb_tx_done();
  mb_request(spanEnd, sizeof(spanEnd));
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x83, 0x02);
  mb_tx_done();
  mb_request(count0, sizeof(count0));
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x83, 0x03);
  mb_tx_done();
  mb_request(count126, sizeof(count126));
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x84, 0x03);
  mb_tx_done();
  mb_request(badBytes, sizeof(badBytes));
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x90, 0x03);
  mb_tx_done();
  mb_request(single, sizeof(single));
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x86, 0x02);
  mb_tx_done();
  CHECK(fake_mb_tx_count == 7);
}

static void test_broadcast_and_other_slave(void)
{
  const uint8_t broadcast[] = {0x00, 0x06, 0x00, MB_HOLD_COM_MB_TIMEOUT, 0x00, 0x07};
  const uint8_t other[] = {0x02, 0x06, 0x00, MB_HOLD_COM_MB_TIMEOUT, 0x00, 0x09};

  mb_setup(0);
  mb_request(broadcast, sizeof(broadcast));
  CHECK(conf.com.mb_timeout == 7);
  CHECK(fake_mb_tx_count == 0);
  mb_request(other, sizeof(other));
  CHECK(conf.com.mb_timeout == 7);
  CHECK(fake_mb_tx_count == 0);
}

static void test_bad_crc_is_dropped(void)
{
  uint8_t frame[8] = {0x01, 0x06, 0x00, MB_HOLD_COM_MB_TIMEOUT, 0x00, 0x03};
  const uint8_t good[] = {0x01, 0x03, 0x00, MB_HOLD_COM_MB_TIMEOUT, 0x00, 0x01};
  uint32_t errors;

  mb_setup(0);
  conf.com.mb_timeout = 1;
  mb_add_crc(frame, 6);
  frame[7] ^= 0xFF;
  errors = MbSlave_GetErrorCount();
  MbSlave_RxFrame(frame, sizeof(frame));
  MbSlave_Handle();
  fake_tick += 10;
  MbSlave_Handle();
  CHECK(fake_mb_tx_count == 0);
  CHECK(conf.com.mb_timeout == 1);
  CHECK(MbSlave_GetErrorCount() == errors + 1);
  mb_request(good, sizeof(good));
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x03, 0x02, 0x00, 0x01);
  mb_tx_done();
}

static void test_split_frame(void)
{
  uint8_t frame[8] = {0x01, 0x03, 0x00, MB_HOLD_COM_MB_TIMEOUT, 0x00, 0x01};

  mb_setup(0);
  mb_add_crc(frame, 6);
  MbSlave_RxFrame(frame, 3);
  MbSlave_Handle();
  CHECK(fake_mb_tx_count == 0);
  fake_tick += 1;
  MbSlave_RxFrame(frame + 3, 5);
  MbSlave_Handle();
  CHECK(fake_mb_tx_count == 1);
  mb_tx_done();
}

static void test_stale_partial_frame(void)
{
  const uint8_t req[] = {0x01, 0x03, 0x00, MB_HOLD_COM_MB_TIMEOUT, 0x00, 0x01};

  mb_setup(0);
  MbSlave_RxFrame(req, 3);
  MbSlave_Handle();
  fake_tick += 10; /* longer than t3.5 at 19200 Bd */
  MbSlave_Handle();
  mb_request(req, sizeof(req));
  CHECK(fake_mb_tx_count == 1);
  CHECK(fake_mb_tx[1] == 0x03 && fake_mb_tx[2] == 0x02);
  mb_tx_done();
}

static void test_rx_during_tx_is_dropped(void)
{
  uint8_t frame[8] = {0x01, 0x03, 0x00, MB_HOLD_COM_MB_TIMEOUT, 0x00, 0x01};

  mb_setup(0);
  mb_request(frame, 6);
  CHECK(fake_mb_tx_count == 1);
  MbSlave_RxFrame(frame, mb_add_crc(frame, 6)); /* still transmitting */
  MbSlave_Handle();
  mb_tx_done();
  CHECK(fake_mb_tx_count == 1);
}

static void test_communication_timeout(void)
{
  const uint8_t req[] = {0x01, 0x03, 0x00, MB_HOLD_COM_MB_TIMEOUT, 0x00, 0x01};

  mb_setup(1); /* 1 s */
  fake_tick += 999;
  MbSlave_Handle();
  CHECK(!MbSlave_IsTimeout());
  fake_tick += 1;
  MbSlave_Handle();
  CHECK(MbSlave_IsTimeout());
  CHECK((conf.sys.status & STAT_BIT_MB_TIMEOUT) != 0);
  mb_request(req, sizeof(req));
  CHECK(!MbSlave_IsTimeout());
  CHECK((conf.sys.status & STAT_BIT_MB_TIMEOUT) == 0);
  mb_tx_done();
}

static void test_reconfigure_after_response(void)
{
  const uint8_t apply[] = {0x01, 0x06, 0x00, MB_HOLD_COM_MB_APPLY, 0x00, 0x01};

  mb_setup(0);
  conf.com.mb_baud_rate = MB_BAUD_115200;
  mb_request(apply, sizeof(apply));
  CHECK(fake_mb_tx_count == 1);
  CHECK(fake_mb_init_count == 1); /* response still goes out at 19200 Bd */
  CHECK(conf.com.mb_apply == 0);
  mb_tx_done();
  CHECK(fake_mb_init_count == 2);
  CHECK(fake_mb_cfg.baudRate == 115200);
}

static void test_address_saturation(void)
{
  MbSlave_Config_t cfg = {0, 19200, MBSLAVE_PARITY_EVEN, 1, 0};

  MbSlave_Init(&cfg);
  CHECK(fake_mb_cfg.address == 1);
  cfg.address = 250;
  MbSlave_Init(&cfg);
  CHECK(fake_mb_cfg.address == 247);
}

void test_modbus(void)
{
  test_crc_known_vector();
  test_read_holding();
  test_read_input_two_words();
  test_write_single();
  test_write_multiple();
  test_exceptions();
  test_broadcast_and_other_slave();
  test_bad_crc_is_dropped();
  test_split_frame();
  test_stale_partial_frame();
  test_rx_during_tx_is_dropped();
  test_communication_timeout();
  test_reconfigure_after_response();
  test_address_saturation();
}
