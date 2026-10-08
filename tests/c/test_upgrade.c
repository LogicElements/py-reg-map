#include "check.h"
#include "config_app.h"
#include "configuration.h"
#include "fake_port.h"
#include "fw_upgrade.h"
#include "lebin.h"
#include "lib_bytes.h"
#include "mb_upgrade.h"
#include "modbus_slave.h"

static void test_session_rules(void)
{
  uint8_t data[32] = {0};

  fake_upgrade_reset();
  (void)Upgrade_Finish(); /* end a session left by earlier tests */
  CHECK(Upgrade_Write(0, data, sizeof(data)) == STATUS_ERROR);
  CHECK(Upgrade_Finish() == STATUS_ERROR);
  CHECK(Upgrade_Begin(0) == STATUS_OK);
  CHECK(Upgrade_IsActive());
  CHECK(Upgrade_Write(0, data, 32) == STATUS_OK);
  CHECK(Upgrade_Write(32, data, 32) == STATUS_OK);
  CHECK(Upgrade_Finish() == STATUS_OK);
  CHECK(fake_verify_size == 64);
  CHECK(!Upgrade_IsActive());
  CHECK(Upgrade_Write(64, data, 32) == STATUS_ERROR);
}

static void test_wrapping_offset_is_rejected(void)
{
  uint8_t data[32] = {0};

  fake_upgrade_reset();
  CHECK(Upgrade_Begin(0) == STATUS_OK);
  CHECK(Upgrade_Write(0xFFFFFFE0u, data, sizeof(data)) == STATUS_ERROR);
  (void)Upgrade_Finish();
}

static void test_short_last_chunk_is_padded(void)
{
  uint8_t data[40];
  uint8_t i;

  for (i = 0; i < sizeof(data); i++)
  {
    data[i] = (uint8_t)(i + 1);
  }
  fake_upgrade_reset();
  CHECK(Upgrade_Begin(0) == STATUS_OK);
  CHECK(Upgrade_Write(0, data, sizeof(data)) == STATUS_OK);
  CHECK(memcmp(fake_flash, data, sizeof(data)) == 0);
  CHECK(fake_flash[40] == 0xFF && fake_flash[63] == 0xFF);
  CHECK(Upgrade_Finish() == STATUS_OK);
  CHECK(fake_verify_size == 40);
}

/* LeBin FW upgrade packet with `len` data bytes at `offset`; runs Lebin_Handle once. */
static void lb_upgrade(uint32_t offset, const uint8_t *data, uint16_t len)
{
  uint8_t pkt[64];

  pkt[0] = LEBIN_START_BYTE;
  pkt[1] = LEBIN_FW_UPGRADE;
  Bytes_PutU16Le(pkt + 2, (uint16_t)(8 + len));
  Bytes_PutU32Le(pkt + 4, offset);
  memcpy(pkt + 8, data, len);
  Lebin_RxBytes(pkt, (uint32_t)(8 + len));
  Lebin_Handle();
}

static void test_lebin_upgrade(void)
{
  uint8_t data[32];
  uint8_t i;

  for (i = 0; i < sizeof(data); i++)
  {
    data[i] = i;
  }
  fake_tick = 1000;
  fake_lebin_reset();
  fake_upgrade_reset();
  Config_Init();
  Lebin_Init();

  lb_upgrade(0, data, 32);
  CHECK(fake_upgrade_begin_count == 1);
  CHECK(memcmp(fake_flash, data, 32) == 0);
  CHECK_BYTES(fake_lebin_tx, fake_lebin_tx_len,
              0x90, 0x7F, 0x0C, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

  lb_upgrade(32, data, 32);
  CHECK_BYTES(fake_lebin_tx, fake_lebin_tx_len,
              0x90, 0x7F, 0x0C, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

  lb_upgrade(64, data, 10); /* not a multiple of 32 */
  CHECK(fake_lebin_tx[8] == 2);

  lb_upgrade(64, data, 0); /* last packet */
  CHECK(fake_verify_count == 1);
  CHECK(fake_verify_size == 64);
  CHECK_BYTES(fake_lebin_tx, fake_lebin_tx_len,
              0x90, 0x7F, 0x0C, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

  fake_verify_result = STATUS_ERROR;
  lb_upgrade(0, data, 32);
  lb_upgrade(32, data, 0);
  CHECK(fake_lebin_tx[8] == 1);
}

/* Modbus helpers (slave 1, no timeout) */

static void mb_setup(void)
{
  MbSlave_Config_t cfg;

  fake_tick = 1000;
  fake_mb_reset();
  fake_upgrade_reset();
  Config_Init();
  conf.com.mb_address = 1;
  conf.com.mb_timeout = 0;
  ConfigApp_ModbusConfig(&cfg);
  MbSlave_Init(&cfg);
}

/* Send a request, then finish the response so that MbUpgr_Handle runs. */
static void mb_transaction(const uint8_t *pdu, uint16_t len)
{
  uint8_t frame[MBSLAVE_FRAME_MAX];

  memcpy(frame, pdu, len);
  Bytes_PutU16Le(frame + len, MbSlave_Crc16(frame, len));
  MbSlave_RxFrame(frame, (uint16_t)(len + 2));
  MbSlave_Handle();
  MbSlave_TxDone();
  MbSlave_Handle();
}

static uint16_t mb_read_status(void)
{
  const uint8_t req[] = {0x01, 0x03, 0x04, 0x0F, 0x00, 0x01}; /* register 1039 */

  mb_transaction(req, sizeof(req));
  return Bytes_GetU16Be(fake_mb_tx + 3);
}

static void test_modbus_upgrade(void)
{
  /* Header at 1000: type 1, mode 0 (erase on the fly), size 64 */
  const uint8_t header[] = {0x01, 0x10, 0x03, 0xE8, 0x00, 0x04, 0x08,
                            0x00, 0x01, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00};
  const uint8_t writeDone[] = {0x01, 0x06, 0x04, 0x10, 0x00, 0x01}; /* register 1040 */
  uint8_t page[7 + 70];
  uint8_t expected[64];
  uint8_t i;

  mb_setup();
  mb_transaction(header, sizeof(header));
  CHECK(fake_upgrade_begin_count == 0); /* the session starts with the page at offset 0 */
  CHECK(MbUpgr_GetType() == 1);
  CHECK(mb_read_status() == MB_UPGR_STATUS_READY);

  /* Page at 1004: page size 64, offset 0, data bytes 0..63 (word bytes swapped on the wire) */
  page[0] = 0x01;
  page[1] = 0x10;
  Bytes_PutU16Be(page + 2, 1004);
  Bytes_PutU16Be(page + 4, 35);
  page[6] = 70;
  Bytes_PutU16Be(page + 7, 64);
  Bytes_PutU16Be(page + 9, 0);
  Bytes_PutU16Be(page + 11, 0);
  for (i = 0; i < 64; i++)
  {
    expected[i] = i;
    page[13 + (i ^ 1u)] = i;
  }
  mb_transaction(page, sizeof(page));
  mb_transaction(writeDone, sizeof(writeDone));
  CHECK(fake_upgrade_begin_count == 1);
  CHECK(fake_upgrade_begin_size == 0);
  CHECK(memcmp(fake_flash, expected, sizeof(expected)) == 0);
  CHECK(fake_verify_count == 1);
  CHECK(fake_verify_size == 64);
  CHECK(mb_read_status() == MB_UPGR_STATUS_DONE_OK);
}

static void test_modbus_upgrade_apply_and_range(void)
{
  const uint8_t apply[] = {0x01, 0x10, 0x03, 0xE8, 0x00, 0x02, 0x04, 0x00, 0x01, 0x00, 0x02};
  const uint8_t beyond[] = {0x01, 0x03, 0x04, 0x12, 0x00, 0x01};  /* 1042 */
  const uint8_t overlap[] = {0x01, 0x03, 0x04, 0x10, 0x00, 0x03}; /* 1040..1042 */

  mb_setup();
  mb_transaction(apply, sizeof(apply));
  CHECK(fake_apply_count == 1);
  mb_transaction(beyond, sizeof(beyond));
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x83, 0x02);
  mb_transaction(overlap, sizeof(overlap));
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x83, 0x02);
}

static void test_modbus_upgrade_failed_page_is_not_finished(void)
{
  /* Header: mode 0, size 0x10040; last page at 0x10000 is beyond the fake flash -> write fails */
  const uint8_t header[] = {0x01, 0x10, 0x03, 0xE8, 0x00, 0x04, 0x08,
                            0x00, 0x01, 0x00, 0x00, 0x00, 0x40, 0x00, 0x01};
  const uint8_t offset[] = {0x01, 0x10, 0x03, 0xEC, 0x00, 0x03, 0x06,
                            0x00, 0x40, 0x00, 0x00, 0x00, 0x01};    /* 1004..1006: 64 B at 0x10000 */
  const uint8_t writeDone[] = {0x01, 0x06, 0x04, 0x10, 0x00, 0x01};

  mb_setup();
  mb_transaction(header, sizeof(header));
  mb_transaction(offset, sizeof(offset));
  mb_transaction(writeDone, sizeof(writeDone));
  CHECK(fake_verify_count == 0);
  CHECK(mb_read_status() == MB_UPGR_STATUS_DONE_ERROR);
}

/* Write one 64-byte page at `offset` (bytes value+0..63), then write-done; no TxDone between. */
static void mb_page(uint32_t offset, uint8_t value)
{
  const uint8_t writeDone[] = {0x01, 0x06, 0x04, 0x10, 0x00, 0x01};
  uint8_t page[7 + 70];
  uint8_t i;

  page[0] = 0x01;
  page[1] = 0x10;
  Bytes_PutU16Be(page + 2, 1004);
  Bytes_PutU16Be(page + 4, 35);
  page[6] = 70;
  Bytes_PutU16Be(page + 7, 64);
  Bytes_PutU16Be(page + 9, (uint16_t)offset);
  Bytes_PutU16Be(page + 11, (uint16_t)(offset >> 16));
  for (i = 0; i < 64; i++)
  {
    page[13 + (i ^ 1u)] = (uint8_t)(value + i);
  }
  mb_transaction(page, sizeof(page));
  mb_transaction(writeDone, sizeof(writeDone));
}

static void test_modbus_header_rewrite_keeps_session(void)
{
  /* Header: mode 0, size 128; a master rewriting the header before every page */
  const uint8_t header[] = {0x01, 0x10, 0x03, 0xE8, 0x00, 0x04, 0x08,
                            0x00, 0x01, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00};

  mb_setup();
  mb_transaction(header, sizeof(header));
  mb_page(0, 0);
  mb_transaction(header, sizeof(header));
  mb_page(64, 64);
  CHECK(fake_upgrade_begin_count == 1);
  CHECK(fake_flash[0] == 0 && fake_flash[63] == 63 && fake_flash[64] == 64);
  CHECK(fake_verify_count == 1);
  CHECK(mb_read_status() == MB_UPGR_STATUS_DONE_OK);
}

static void test_modbus_status_busy_until_page_written(void)
{
  const uint8_t header[] = {0x01, 0x10, 0x03, 0xE8, 0x00, 0x04, 0x08,
                            0x00, 0x01, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00};
  const uint8_t writeDone[] = {0x01, 0x06, 0x04, 0x10, 0x00, 0x01};
  uint8_t frame[MBSLAVE_FRAME_MAX];
  uint8_t status[2];

  mb_setup();
  mb_transaction(header, sizeof(header));
  memcpy(frame, writeDone, sizeof(writeDone));
  Bytes_PutU16Le(frame + 6, MbSlave_Crc16(frame, 6));
  MbSlave_RxFrame(frame, 8);
  MbSlave_Handle(); /* request processed, page not programmed yet (response pending) */
  MbUpgr_ReadRegisters(1039, 1, status);
  CHECK(Bytes_GetU16Be(status) == MB_UPGR_STATUS_BUSY);
  MbSlave_TxDone();
  MbSlave_Handle();
}

static void test_modbus_short_last_page(void)
{
  /* Header: mode 0, size 40; one page of 40 bytes */
  const uint8_t header[] = {0x01, 0x10, 0x03, 0xE8, 0x00, 0x04, 0x08,
                            0x00, 0x01, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00};
  const uint8_t pageSize[] = {0x01, 0x06, 0x03, 0xEC, 0x00, 0x28}; /* 1004 = 40 */
  const uint8_t writeDone[] = {0x01, 0x06, 0x04, 0x10, 0x00, 0x01};

  mb_setup();
  mb_transaction(header, sizeof(header));
  mb_transaction(pageSize, sizeof(pageSize));
  mb_transaction(writeDone, sizeof(writeDone));
  CHECK(fake_verify_count == 1);
  CHECK(fake_verify_size == 40);
  CHECK(mb_read_status() == MB_UPGR_STATUS_DONE_OK);
}

void test_upgrade(void)
{
  test_session_rules();
  test_wrapping_offset_is_rejected();
  test_short_last_chunk_is_padded();
  test_lebin_upgrade();
  test_modbus_upgrade();
  test_modbus_upgrade_apply_and_range();
  test_modbus_upgrade_failed_page_is_not_finished();
  test_modbus_header_rewrite_keeps_session();
  test_modbus_status_busy_until_page_written();
  test_modbus_short_last_page();
}
