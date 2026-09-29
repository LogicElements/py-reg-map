import pytest
from helpers import errors, load, map_yaml

from regmap.model import MapError, enum_numbers, load_map, load_map_text

ONE = "SYS: {{code: 0, registers: [{reg}]}}"


def reg_errors(reg: str) -> list[str]:
    return errors(ONE.format(reg=reg))


def test_minimal_map_loads_with_defaults():
    rmap = load(ONE.format(reg="{name: UPTIME, type: INT, access: RO, size: 4}"))
    assert rmap.device.name == "Dev"
    assert rmap.device.c_prefix == "CONF_"
    assert rmap.device.c_storage == "CONF_REG"
    assert rmap.generator.outputs.reg_map is None
    reg = rmap.blocks["SYS"].registers[0]
    assert (reg.name, reg.type, reg.access, reg.size) == ("UPTIME", "INT", "RO", 4)


def test_unknown_key_is_reported_with_register_name():
    assert reg_errors("{name: A, type: INT, access: RO, size: 4, defualt: 1}") == [
        "blocks.SYS.registers[0] (A).defualt: unknown key"
    ]


def test_duplicate_yaml_key_is_an_error():
    text = map_yaml("SYS:\n  code: 0\n  code: 1\n  registers: []")
    with pytest.raises(MapError) as exc:
        load_map_text(text)
    assert "found duplicate key 'code'" in exc.value.errors[0]


def test_yaml_syntax_error_names_the_source():
    with pytest.raises(MapError) as exc:
        load_map_text("device: [", source="map.yaml")
    assert exc.value.errors[0].startswith("map.yaml: ")


def test_top_level_must_be_a_mapping():
    with pytest.raises(MapError, match="top level must be a mapping"):
        load_map_text("- 1\n- 2\n")


def test_unquoted_boolean_text_gets_a_hint():
    msgs = reg_errors("{name: A, type: INT, access: RO, size: 4, label: ON}")
    assert msgs == [
        "blocks.SYS.registers[0] (A).label: YAML read this unquoted value as true/false;"
        " put the text in quotes"
    ]


def test_numeric_text_is_read_as_text():
    rmap = load(ONE.format(reg="{name: A, type: INT, access: RO, size: 4, label: 9600}"))
    assert rmap.blocks["SYS"].registers[0].label == "9600"


def test_label_must_be_single_line():
    assert (
        "must be a single line"
        in reg_errors('{name: A, type: INT, access: RO, size: 4, label: "a\\nb"}')[0]
    )


@pytest.mark.parametrize(
    ("reg", "message"),
    [
        (
            "{name: A, type: INT, access: RO, size: 3}",
            "size 3 is not allowed for INT; use 1, 2, 4, 8",
        ),
        ("{name: A, type: FLOAT, access: RO, size: 2}", "size 2 is not allowed for FLOAT; use 4"),
        ("{name: A, type: ENUM, access: RO, size: 8, values: [{name: V}]}", "not allowed for ENUM"),
        ("{name: A, type: INT, access: XX, size: 4}", "Input should be"),
        ("{name: 1A, type: INT, access: RO, size: 4}", "should match pattern"),
    ],
)
def test_register_field_rules(reg, message):
    assert message in reg_errors(reg)[0]


def test_string_accepts_any_size():
    load(ONE.format(reg="{name: S, type: STRING, access: RO, size: 13}"))


def test_enum_needs_values():
    assert reg_errors("{name: A, type: ENUM, access: RW, size: 1}") == [
        "blocks.SYS.registers[0] (A): ENUM register needs values"
    ]


@pytest.mark.parametrize(
    ("reg", "message"),
    [
        ("{name: A, type: INT, access: RW, size: 1, values: [{name: V}]}", "only allowed for ENUM"),
        ("{name: A, type: ENUM, access: RW, size: 1, values: []}", "at least one value"),
        (
            "{name: A, type: ENUM, access: RW, size: 1, values: [{name: V}, {name: V}]}",
            "duplicate value name V",
        ),
        (
            "{name: A, type: ENUM, access: RW, size: 1, values: [{name: V, value: 1}, {name: W, value: 1}]}",
            "duplicate enum value 1",
        ),
        (
            "{name: A, type: ENUM, access: RW, size: 1, values: [{name: V, value: 256}]}",
            "enum value 256 does not fit into 1 byte(s)",
        ),
        (
            "{name: A, type: ENUM, access: RW, size: 1, min: 0, values: [{name: V}]}",
            "min is derived",
        ),
    ],
)
def test_enum_rules(reg, message):
    assert message in " ".join(reg_errors(reg))


def test_enum_numbers_continue_after_explicit_value():
    rmap = load(
        ONE.format(
            reg="{name: A, type: ENUM, access: RW, size: 1, values: "
            "[{name: V0}, {name: V5, value: 5}, {name: V6}]}"
        )
    )
    assert enum_numbers(rmap.blocks["SYS"].registers[0].values) == [0, 5, 6]


@pytest.mark.parametrize(
    ("reg", "message"),
    [
        (
            "{name: A, type: INT, access: RO, size: 4, bits: [{name: B, bit: 0}]}",
            "only allowed for BIN",
        ),
        (
            "{name: A, type: BIN, access: RO, size: 1, bits: [{name: B, bit: 8}]}",
            "bit 8 (B) does not fit",
        ),
        (
            "{name: A, type: BIN, access: RO, size: 1, bits: [{name: B, bit: 1}, {name: C, bit: 1}]}",
            "bit 1 is defined twice",
        ),
        ("{name: A, type: BIN, access: RO, size: 1, bits: [{name: B}]}", "bit: Field required"),
    ],
)
def test_bit_rules(reg, message):
    assert message in " ".join(reg_errors(reg))


def test_bin_without_bits_is_fine():
    load(ONE.format(reg="{name: A, type: BIN, access: RO, size: 4}"))


@pytest.mark.parametrize(
    ("reg", "message"),
    [
        (
            "{name: A, type: ENUM, access: RW, size: 1, default: X, values: [{name: V}]}",
            "ENUM default must be one of its value names: V",
        ),
        (
            "{name: A, type: ENUM, access: RW, size: 1, default: 0, values: [{name: V}]}",
            "ENUM default",
        ),
        ("{name: A, type: STRING, access: RW, size: 3, default: abc}", "needs 4 bytes"),
        (
            "{name: A, type: STRING, access: RW, size: 4, default: 12}",
            "STRING default must be text",
        ),
        ("{name: A, type: INT, access: RW, size: 1, default: 256}", "does not fit into 1 unsigned"),
        ("{name: A, type: INT, access: RW, size: 1, default: -1}", "does not fit into 1 unsigned"),
        (
            "{name: A, type: INT, access: RW, size: 2, default: 1.5}",
            "INT default must be an integer",
        ),
        ("{name: A, type: INT, access: RW, size: 2, default: abc}", "INT default must be a number"),
        (
            "{name: A, type: INT, access: RW, size: 2, min: 5, default: 4}",
            "default 4 is below min 5",
        ),
        ("{name: A, type: FLOAT, access: RW, size: 4, max: 1, default: 1.5}", "above max 1"),
        ("{name: A, type: INT, access: RW, size: 2, min: 5, max: 1}", "max 1 is lower than min 5"),
    ],
)
def test_default_and_range_rules(reg, message):
    assert message in " ".join(reg_errors(reg))


def test_valid_defaults():
    rmap = load(
        """
        SYS:
          code: 0
          registers:
            - {name: E, type: ENUM, access: RW, size: 1, default: W, values: [{name: V}, {name: W}]}
            - {name: S, type: STRING, access: RW, size: 4, default: abc}
            - {name: F, type: FLOAT, access: RW, size: 4, min: -1, max: 2.5, default: -0.5}
            - {name: H, type: INT, access: RW, size: 2, max: 0x3FF, default: 0x10}
        """
    )
    assert [r.default for r in rmap.blocks["SYS"].registers] == ["W", "abc", -0.5, 16]


@pytest.mark.parametrize(
    ("modbus", "message"),
    [
        ("true", "Modbus is on by default"),
        ("{format: F32}", "format F32 is only allowed for FLOAT"),
        ("{address: 70000}", "less than or equal to 65535"),
        ("{adress: 1}", "modbus.adress: unknown key"),
    ],
)
def test_modbus_rules(modbus, message):
    assert message in " ".join(
        reg_errors(f"{{name: A, type: INT, access: RW, size: 2, modbus: {modbus}}}")
    )


def test_block_rules():
    assert "should match pattern" in errors("sys: {code: 0, registers: []}")[0]
    assert "less than or equal to 25" in errors("SYS: {code: 26, registers: []}")[0]


def test_device_name_must_be_an_identifier():
    assert (
        "should match pattern" in errors("SYS: {code: 0, registers: []}", device="name: my-dev")[0]
    )


def test_load_map_accepts_utf8_bom(tmp_path):
    path = tmp_path / "map.yaml"
    path.write_bytes(b"\xef\xbb\xbf" + map_yaml("SYS: {code: 0, registers: []}").encode())
    assert load_map(path).device.name == "Dev"
