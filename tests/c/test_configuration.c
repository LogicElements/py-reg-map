#include "check.h"
#include "configuration.h"
#include "lib_bytes.h"

static void test_init_checks_layout(void)
{
  CHECK(Config_Init() == STATUS_OK); /* fails when reg_map.h and the compiler disagree */
  CHECK(conf.com.mb_timeout == 10);  /* factory value of vms1511 */
}

static void test_check_limits(void)
{
  CHECK(Config_CheckLimits(CONF_SYS_UPTIME) == STATUS_OK);
  CHECK(Config_CheckLimits(CONF_COM_MB_TIMEOUT) == STATUS_OK);
  CHECK(Config_CheckLimits(0x7F000012u) != STATUS_OK);  /* block beyond the map */
  CHECK(Config_CheckLimits(0x00100012u) != STATUS_OK);  /* address 256 beyond block SYS */
}

static void test_stream_round_trip_on_unaligned_buffer(void)
{
  uint8_t buffer[CONF_REG_FLASH_LENGTH + 64];
  uint8_t *stream = buffer + 1; /* deliberately unaligned */
  uint32_t length = 0;

  Config_Init();
  conf.com.mb_timeout = 0x1234;
  CHECK(Config_FillStream(stream, &length, sizeof(buffer) - 1) == STATUS_OK);
  CHECK(length == CONF_REG_FLASH_LENGTH);
  CHECK(Bytes_GetU32Le(stream) == CONF_SYS_REGMAP_VERSION);

  conf.com.mb_timeout = 0;
  CHECK(Config_ReadStream(stream, length) == STATUS_OK);
  CHECK(conf.com.mb_timeout == 0x1234);
}

static void test_fill_stream_reports_small_buffer(void)
{
  uint8_t buffer[8];
  uint32_t length = 0;

  CHECK(Config_FillStream(buffer, &length, sizeof(buffer)) == STATUS_ERROR);
  CHECK(length == 8); /* the version entry fits, the rest does not */
}

static void test_read_stream_rejects_other_major_version(void)
{
  uint8_t stream[8];

  Config_Init();
  Bytes_PutU32Le(stream, CONF_SYS_REGMAP_VERSION);
  Bytes_PutU32Le(stream + 4, CONF_INT(CONF_SYS_REGMAP_VERSION) + 0x00010000u);
  CHECK(Config_ReadStream(stream, sizeof(stream)) == STATUS_ERROR);
}

static void test_read_stream_skips_unknown_and_stops_on_truncation(void)
{
  uint8_t stream[64];
  uint32_t idx = 0;

  Config_Init();
  Bytes_PutU32Le(stream + idx, CONF_SYS_REGMAP_VERSION);
  Bytes_PutU32Le(stream + idx + 4, CONF_INT(CONF_SYS_REGMAP_VERSION));
  idx += 8;
  Bytes_PutU32Le(stream + idx, CONF_SYS_UPTIME); /* not a flash register: skipped */
  Bytes_PutU32Le(stream + idx + 4, 77);
  idx += 8;
  Bytes_PutU32Le(stream + idx, CONF_COM_MB_TIMEOUT);
  Bytes_PutU16Le(stream + idx + 4, 42);
  idx += 6;
  conf.sys.uptime = 5;
  CHECK(Config_ReadStream(stream, idx) == STATUS_OK);
  CHECK(conf.sys.uptime == 5);
  CHECK(conf.com.mb_timeout == 42);
  CHECK(Config_ReadStream(stream, idx - 1) == STATUS_ERROR); /* last value truncated */
}

static void test_need_to_sync_without_synced_registers(void)
{
  uint8_t data[16];
  uint16_t length = 99;

  CHECK(Config_NeedToSync(data, &length) == STATUS_ERROR); /* vms1511 has none */
  CHECK(length == 0);
}

void test_configuration(void)
{
  test_init_checks_layout();
  test_check_limits();
  test_stream_round_trip_on_unaligned_buffer();
  test_fill_stream_reports_small_buffer();
  test_read_stream_rejects_other_major_version();
  test_read_stream_skips_unknown_and_stops_on_truncation();
  test_need_to_sync_without_synced_registers();
}
