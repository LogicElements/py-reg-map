from helpers import resolved

from regmap.generators import name_width
from regmap.generators.c_regmap import (
    REG_BITS,
    REG_FACTORY,
    REG_MAP,
    REG_PARAMS,
    REG_STORAGE,
    REG_TYPEDEFS,
    c_float,
    c_string,
    header_fragments,
    source_fragments,
)

MAP = """
SYS:
  code: 0
  registers:
    - {name: UPTIME, type: INT, access: RO, size: 4, label: Uptime, default: 0}
    - {name: STATUS, type: BIN, access: RO, size: 4, bits: [{name: stat_err, bit: 0}, {name: STAT_A_VERY_LONG_BIT_NAME_X, bit: 17}]}
FACT:
  code: 1
  registers:
    - {name: SERIAL_NUMBER, type: INT, access: RO, size: 4}
COM:
  code: 3
  registers:
    - {name: MODE, type: ENUM, access: RWF, size: 1, default: M_B, values: [{name: M_A}, {name: M_B, value: 5}]}
    - {name: NAME, type: STRING, access: RWIF, size: 6, address: 2, default: ab"c}
    - {name: GAIN, type: FLOAT, access: RWF, size: 4, default: 2}
    - {name: SHORT, type: INT, access: RW, size: 2, default: 7}
"""


def test_name_width_counts_registers_bits_and_enum_values():
    assert name_width(resolved(MAP)) == len("STAT_A_VERY_LONG_BIT_NAME_X")


def test_define_map():
    lines = header_fragments(resolved(MAP))[REG_MAP].splitlines()
    # width 27 (longest bit name): 1 + 27 - len("SYS_UPTIME") = 18 spaces
    assert lines[0] == "#define CONF_SYS_UPTIME" + " " * 18 + "0x00000112u  ///< Uptime"
    assert lines[1] == "#define CONF_SYS_STATUS" + " " * 18 + "0x00004012u  ///< "
    assert len(lines) == 7


def test_define_bits_are_upper_case_with_min_three_spaces():
    assert header_fragments(resolved(MAP))[REG_BITS] == (
        "#define STAT_ERR                      (1 << (0))\n"
        "#define STAT_A_VERY_LONG_BIT_NAME_X   (1 << (17))\n"
    )


def test_params():
    lines = header_fragments(resolved(MAP))[REG_PARAMS].splitlines()
    assert lines[2] == "#define CONF_REG_CALIB_NUMBER      (2)"  # serial number + NAME
    assert lines[4] == "#define CONF_REG_FLASH_NUMBER      (2)"  # MODE, GAIN
    assert lines[6] == "#define CONF_REG_CALIB_LENGTH      (18)"  # 8 + (6 + 4)
    assert lines[8] == "#define CONF_REG_FLASH_LENGTH      (13)"  # (1 + 4) + (4 + 4)
    assert lines[10] == ""
    assert lines[11] == (
        "#define CONF_DIM_CONDITION ((sizeof(conf_reg_sys_t) != 8) || "
        "(sizeof(conf_reg_fact_t) != 4) || (sizeof(conf_reg_com_t) != 16) || 0)"
    )


def test_typedefs():
    text = header_fragments(resolved(MAP))[REG_TYPEDEFS]
    assert text.startswith("\ntypedef enum\n{\n  M_A = 0,\n  M_B = 5,\n}com_mode_t ;\n\n")
    assert (
        "typedef struct __packed __aligned(4)\n{\n"
        "  com_mode_t mode;\n"
        "  uint8_t reserved0[1];\n"
        "  uint8_t name[6];\n"
        "  float gain;\n"
        "  uint16_t short;\n"
        "}conf_reg_com_t;\n\n"
    ) in text
    assert (
        "\n\ntypedef struct \n{\n  conf_reg_sys_t sys;\n  conf_reg_fact_t fact;\n  uint32_t res3;\n"
        in text
    )
    assert text.endswith("  uint32_t res26;\n}\nconf_reg_t;\n\n")


def test_custom_storage_name():
    rmap = resolved(
        "SYS: {code: 0, registers: [{name: A, type: INT, access: RO, size: 4}]}",
        device="name: Dev, c_storage: MY_REG, c_prefix: REG_",
    )
    header = header_fragments(rmap)
    assert "sizeof(my_reg_sys_t)" in header[REG_PARAMS]
    assert header[REG_TYPEDEFS].endswith("}\nmy_reg_t;\n\n")
    assert header[REG_MAP].startswith("#define REG_SYS_A ")
    assert source_fragments(rmap)[REG_STORAGE].startswith("my_reg_t conf;\n\n\n")


def test_storage_arrays():
    lines = source_fragments(resolved(MAP))[REG_STORAGE].splitlines()
    assert lines[:3] == ["conf_reg_t conf;", "", ""]
    assert lines[3] == (
        "uint8_t* const CONF_REG[CONF_REG_BLOCK_NUMBER] = {(uint8_t*)&conf.sys, "
        "(uint8_t*)&conf.fact, NULL, (uint8_t*)&conf.com, " + "NULL, " * 22 + "};"
    )
    assert lines[5:7] == [
        "const uint32_t CONF_REG_LIMIT[CONF_REG_BLOCK_NUMBER] = {",
        "8, 4, 0, 14, " + "0, " * 22 + "};",
    ]
    assert lines[8:10] == [
        "const uint32_t CONF_REG_FLASH[CONF_REG_FLASH_NUMBER] = {",
        "CONF_COM_MODE, CONF_COM_GAIN, };",
    ]
    assert lines[11:13] == ["const uint32_t CONF_REG_LOGGER[CONF_REG_LOGGER_NUMBER] = {", "};"]
    assert lines[15] == "CONF_FACT_SERIAL_NUMBER, CONF_COM_NAME, };"
    assert lines[-3:] == ["const uint32_t CONF_REG_SYNCED[CONF_REG_SYNCED_NUMBER] = {", "};", ""]


def test_factory_values():
    assert source_fragments(resolved(MAP))[REG_FACTORY] == (
        "  CONF_INT(CONF_SYS_UPTIME)                    = 0;\n"
        "  CONF_BYTE(CONF_COM_MODE)                     = 5;\n"
        '  memcpy(CONF_PTR(CONF_COM_NAME), "ab\\"c", sizeof("ab\\"c"));\n'
        "  CONF_FLOAT(CONF_COM_GAIN)                    = 2.0;\n"
        "  CONF_SHORT(CONF_COM_SHORT)                   = 7;\n"
    )


def test_c_float_and_c_string():
    assert [c_float(v) for v in (2, 1.25, 0.1234567, -0.5, 1e-9)] == [
        "2.0", "1.25", "0.123457", "-0.5", "0.0",
    ]  # fmt: skip
    assert c_string('a\\b"\n') == '"a\\\\b\\"\\n"'


def test_block_without_registers_is_an_unused_slot():
    rmap = resolved(
        "SYS: {code: 0, registers: []}\nCOM: {code: 3, registers: [{name: A, type: INT, access: RO, size: 2}]}"
    )
    header = header_fragments(rmap)
    assert "conf_reg_sys_t" not in header[REG_PARAMS] + header[REG_TYPEDEFS]
    assert "  uint32_t res1;\n" in header[REG_TYPEDEFS]
    storage = source_fragments(rmap)[REG_STORAGE].splitlines()
    assert storage[3].startswith(
        "uint8_t* const CONF_REG[CONF_REG_BLOCK_NUMBER] = {NULL, NULL, NULL, (uint8_t*)&conf.com, "
    )
    assert storage[6].startswith("0, 0, 0, 2, 0, ")
