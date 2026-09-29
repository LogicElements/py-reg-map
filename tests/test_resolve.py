import pytest
from helpers import errors, resolved

from regmap.codes import RegType

SYS = """
SYS:
  code: 0
  registers:
    - {name: UPTIME, type: INT, access: RO, size: 4}
    - {name: FLAGS, type: BIN, access: RO, size: 2, bits: [{name: F_A, bit: 0}]}
    - {name: GAP, type: INT, access: RW, size: 1, address: 10}
    - {name: TAIL, type: INT, access: RW, size: 2}
"""


def test_addresses_follow_previous_register_and_allow_gaps():
    block = resolved(SYS).blocks[0]
    assert [(r.full_name, r.address) for r in block.registers] == [
        ("SYS_UPTIME", 0),
        ("SYS_FLAGS", 4),
        ("SYS_GAP", 10),
        ("SYS_TAIL", 11),
    ]
    assert block.end == 13


def test_ids_are_composed_from_resolved_addresses():
    regs = list(resolved(SYS).registers)
    assert [f"0x{r.id:08X}" for r in regs] == [
        "0x00000112",
        "0x00004011",
        "0x0000A150",
        "0x0000B151",
    ]


def test_overlap_and_block_overflow_are_errors():
    msgs = errors(
        """
        SYS:
          code: 0
          registers:
            - {name: A, type: INT, access: RO, size: 4}
            - {name: B, type: INT, access: RO, size: 4, address: 2}
            - {name: C, type: STRING, access: RO, size: 10, address: 4090}
        """
    )
    assert msgs == [
        "blocks.SYS.registers[1] (B).address: 2 overlaps the previous register ending at 4",
        "blocks.SYS.registers[2] (C): ends at 4100, beyond the 4096-byte block",
    ]


def test_blocks_are_ordered_by_code_not_yaml_order():
    rmap = resolved(
        """
        COM: {code: 3, registers: [{name: A, type: INT, access: RO, size: 2}]}
        SYS: {code: 0, registers: [{name: B, type: INT, access: RO, size: 2}]}
        """
    )
    assert [b.abbrev for b in rmap.blocks] == ["SYS", "COM"]
    assert rmap.block_by_code(3).abbrev == "COM"
    assert rmap.block_by_code(7) is None


def test_duplicate_block_code_is_an_error():
    assert errors("A: {code: 1, registers: []}\nB: {code: 1, registers: []}") == [
        "blocks.B.code: 1 is already used by A"
    ]


def test_full_names_must_be_unique_ignoring_case():
    msgs = errors(
        """
        SYS:
          code: 0
          registers:
            - {name: MODE, type: INT, access: RO, size: 2}
            - {name: Mode, type: INT, access: RO, size: 2}
        """
    )
    assert msgs == [
        "blocks.SYS.registers[1] (Mode).name: name SYS_Mode is already used at"
        " blocks.SYS.registers[0] (MODE).name"
    ]


def test_bit_and_enum_names_are_unique_across_the_map():
    msgs = errors(
        """
        SYS:
          code: 0
          registers:
            - {name: A, type: BIN, access: RO, size: 1, bits: [{name: SAME, bit: 0}]}
            - {name: B, type: ENUM, access: RW, size: 1, values: [{name: SAME}]}
        """
    )
    assert msgs == [
        "blocks.SYS.registers[1] (B).values[0]: name SAME is already used at"
        " blocks.SYS.registers[0] (A).bits[0]"
    ]


def test_enum_default_and_range_are_resolved_to_numbers():
    reg = next(
        resolved(
            """
            SYS:
              code: 0
              registers:
                - {name: E, type: ENUM, access: RW, size: 1, default: C, values: [{name: A, value: 2}, {name: B}, {name: C, value: 7}]}
            """
        ).registers
    )
    assert [(v.name, v.value) for v in reg.values] == [("A", 2), ("B", 3), ("C", 7)]
    assert (reg.default, reg.min, reg.max, reg.range_min, reg.range_max) == (7, None, None, 2, 7)


def test_c_types():
    rmap = resolved(
        """
        COM:
          code: 3
          registers:
            - {name: MB_BAUD_RATE, type: ENUM, access: RW, size: 1, values: [{name: B9600}]}
            - {name: GAIN, type: FLOAT, access: RW, size: 4}
            - {name: B1, type: INT, access: RW, size: 1}
            - {name: B8, type: INT, access: RW, size: 8}
            - {name: TXT4, type: STRING, access: RW, size: 4}
            - {name: TXT16, type: STRING, access: RW, size: 16}
            - {name: TXT6, type: STRING, access: RW, size: 6}
        """
    )
    assert [(r.c_type, r.c_member, r.c_array) for r in rmap.registers] == [
        ("com_mb_baud_rate_t", "mb_baud_rate", None),
        ("float", "gain", None),
        ("uint8_t", "b1", None),
        ("uint64_t", "b8", None),
        ("uint32_t", "txt4", None),
        ("uint8_t", "txt16", 16),
        ("uint8_t", "txt6", 6),
    ]


MODBUS = """
SYS:
  code: 0
  registers:
    - {name: I4, type: INT, access: RO, size: 4}
    - {name: H1, type: INT, access: RW, size: 1}
    - {name: H5, type: STRING, access: RW, size: 5}
    - {name: E, type: ENUM, access: RWF, size: 1, values: [{name: E0}]}
    - {name: F, type: FLOAT, access: RW, size: 4}
    - {name: F32, type: FLOAT, access: RW, size: 4, modbus: {format: F32}}
    - {name: HIDDEN, type: INT, access: RW, size: 2, modbus: false}
    - {name: FIXED, type: INT, access: ROF, size: 2, modbus: {address: 100}}
    - {name: NEXT, type: INT, access: RO, size: 2}
"""


def test_modbus_allocation():
    rmap = resolved(MODBUS)
    placed = {
        r.name: r.modbus and (r.modbus.space, r.modbus.addresses, r.modbus.format)
        for r in rmap.registers
    }
    assert placed == {
        "I4": ("INPUT", (0, 1), "INT"),
        "H1": ("HOLD", (0,), "INT"),
        "H5": ("HOLD", (1, 2, 3), "STRING"),
        "E": ("HOLD", (4,), "ENUM"),
        "F": ("HOLD", (5,), "FLOAT"),
        "F32": ("HOLD", (6, 7), "FLOAT32"),
        "HIDDEN": None,
        "FIXED": ("INPUT", (100,), "INT"),
        "NEXT": ("INPUT", (101,), "INT"),
    }
    assert [r.modbus.float_x10 for r in rmap.registers if r.type is RegType.FLOAT] == [True, False]
    assert (rmap.input_last, rmap.hold_last) == (101, 7)


def test_empty_modbus_space_has_last_minus_one():
    rmap = resolved("SYS: {code: 0, registers: [{name: A, type: INT, access: RO, size: 2}]}")
    assert (rmap.input_last, rmap.hold_last) == (0, -1)


def test_modbus_address_collision_is_an_error():
    msgs = errors(
        """
        SYS:
          code: 0
          registers:
            - {name: A, type: INT, access: RW, size: 4}
            - {name: B, type: INT, access: RW, size: 2, modbus: {address: 1}}
        """
    )
    assert msgs == ["blocks.SYS.registers[1] (B).modbus: HOLD address 1 is already used by SYS_A"]


def test_modbus_address_beyond_range_is_an_error():
    msgs = errors(
        "SYS: {code: 0, registers: [{name: A, type: INT, access: RW, size: 4, modbus: {address: 65535}}]}"
    )
    assert msgs == ["blocks.SYS.registers[0] (A).modbus: address 65536 is beyond 65535"]


FLASH_CALIB = """
FACT:
  code: 1
  registers:
    - {name: SERIAL_NUMBER, type: INT, access: RO, size: 4}
    - {name: VERSION, type: INT, access: ROF, size: 4}
CALIB:
  code: 4
  registers:
    - {name: GAIN, type: FLOAT, access: RWIF, size: 4}
    - {name: MODE, type: ENUM, access: RWF, size: 1, values: [{name: M0}]}
"""


def test_flash_and_calib_lists():
    rmap = resolved(FLASH_CALIB)
    assert [r.full_name for r in rmap.flash] == ["FACT_VERSION", "CALIB_MODE"]
    assert rmap.calib_serial.full_name == "FACT_SERIAL_NUMBER"
    assert [r.full_name for r in rmap.calib] == ["CALIB_GAIN"]


def test_calib_serial_is_optional():
    rmap = resolved("SYS: {code: 0, registers: [{name: A, type: INT, access: RWIF, size: 4}]}")
    assert rmap.calib_serial is None
    assert [r.full_name for r in rmap.calib] == ["SYS_A"]


@pytest.mark.parametrize("access", ["RWIF"])
def test_serial_number_is_not_listed_twice(access):
    rmap = resolved(
        f"FACT: {{code: 1, registers: [{{name: SERIAL_NUMBER, type: INT, access: {access}, size: 4}}]}}"
    )
    assert rmap.calib_serial is not None
    assert rmap.calib == ()
