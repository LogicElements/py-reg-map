from helpers import resolved

from regmap.generators.c_modbus import (
    HOLD_DEFINE,
    INPUT_DEFINE,
    READ_HOLD,
    READ_INPUT,
    WRITE_HOLD,
    header_fragments,
    source_fragments,
)

MAP = """
SYS:
  code: 0
  registers:
    - {name: UPTIME, type: INT, access: RO, size: 4}
    - {name: MODE, type: ENUM, access: RWF, size: 1, values: [{name: M0}]}
    - {name: TEMP, type: FLOAT, access: RW, size: 4}
    - {name: EXACT, type: FLOAT, access: RW, size: 4, modbus: {format: F32}}
    - {name: HIDDEN, type: INT, access: RW, size: 2, modbus: false}
    - {name: LEVEL, type: INT, access: RW, size: 2, modbus: {address: 100}}
"""


def test_defines():
    header = header_fragments(resolved(MAP))
    # width = len("SYS_HIDDEN") = 10 -> the define name is padded to 10 + 14 characters
    assert header[INPUT_DEFINE] == (
        "#define MB_INPUT_FIRST    0\n"
        "\n"
        "#define MB_INPUT_SYS_UPTIME_0   0u\n"
        "#define MB_INPUT_SYS_UPTIME_1   1u\n"
        "\n"
        "#define MB_INPUT_LAST     1\n"
    )
    assert header[HOLD_DEFINE].splitlines() == [
        "#define MB_HOLD_FIRST     0",
        "",
        "#define MB_HOLD_SYS_MODE        0u",
        "#define MB_HOLD_SYS_TEMP        1u",
        "#define MB_HOLD_SYS_EXACT_0     2u",
        "#define MB_HOLD_SYS_EXACT_1     3u",
        "#define MB_HOLD_SYS_LEVEL       100u",
        "",
        "#define MB_HOLD_LAST      100",
    ]


def test_empty_space_defines():
    header = header_fragments(
        resolved("SYS: {code: 0, registers: [{name: A, type: INT, access: RO, size: 2}]}")
    )
    assert header[HOLD_DEFINE] == "#define MB_HOLD_FIRST     0\n\n\n#define MB_HOLD_LAST      -1\n"


def test_reads():
    source = source_fragments(resolved(MAP))
    assert source[READ_INPUT] == (
        "    case MB_INPUT_SYS_UPTIME_0:\n"
        "      *value = *((uint16_t *)CONF_PTR(CONF_SYS_UPTIME) + 0);\n"
        "      break;\n"
        "    case MB_INPUT_SYS_UPTIME_1:\n"
        "      *value = *((uint16_t *)CONF_PTR(CONF_SYS_UPTIME) + 1);\n"
        "      break;\n"
    )
    hold = source[READ_HOLD].splitlines()
    assert hold[:6] == [
        "    case MB_HOLD_SYS_MODE:",
        "      *value = conf.sys.mode;",
        "      break;",
        "    case MB_HOLD_SYS_TEMP:",
        "      *value = (int16_t)(10 * conf.sys.temp);",
        "      break;",
    ]
    assert "      *value = *((uint16_t *)CONF_PTR(CONF_SYS_EXACT) + 1);" in hold
    assert "HIDDEN" not in source[READ_HOLD]


def test_writes():
    assert source_fragments(resolved(MAP))[WRITE_HOLD] == (
        "    case MB_HOLD_SYS_MODE:\n"
        "      conf.sys.mode = (sys_mode_t)value;\n"
        "      id = CONF_SYS_MODE;\n"
        "      break;\n"
        "    case MB_HOLD_SYS_TEMP:\n"
        "      conf.sys.temp = ((float)((int16_t)value)) / 10;\n"
        "      id = CONF_SYS_TEMP;\n"
        "      break;\n"
        "    case MB_HOLD_SYS_EXACT_0:\n"
        "      *((uint16_t *)CONF_PTR(CONF_SYS_EXACT) + 0) = value;\n"
        "      break;\n"
        "    case MB_HOLD_SYS_EXACT_1:\n"
        "      *((uint16_t *)CONF_PTR(CONF_SYS_EXACT) + 1) = value;\n"
        "      id = CONF_SYS_EXACT;\n"
        "      break;\n"
        "    case MB_HOLD_SYS_LEVEL:\n"
        "      conf.sys.level = value;\n"
        "      id = CONF_SYS_LEVEL;\n"
        "      break;\n"
    )


def test_long_register_keeps_define_columns_apart():
    rmap = resolved("SYS: {code: 0, registers: [{name: TEXT, type: STRING, access: RO, size: 32}]}")
    lines = header_fragments(rmap)[INPUT_DEFINE].splitlines()
    assert len(lines) == 2 + 16 + 2
    assert lines[2] == "#define MB_INPUT_SYS_TEXT_0   0u"
    assert lines[17] == "#define MB_INPUT_SYS_TEXT_15  15u"


def test_huge_register_still_has_a_space_before_the_address():
    rmap = resolved(
        "SYS: {code: 0, registers: [{name: TEXT, type: STRING, access: RO, size: 2048}]}"
    )
    lines = header_fragments(rmap)[INPUT_DEFINE].splitlines()
    assert lines[-3] == "#define MB_INPUT_SYS_TEXT_1023 1023u"
