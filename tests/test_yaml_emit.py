import pytest
import yaml

from regmap.importers.yaml_emit import HexInt, emit_map, scalar
from regmap.model import load_map_text


@pytest.mark.parametrize(
    ("value", "flow", "expected"),
    [
        ("NONE", False, "NONE"),
        ("9600", False, '"9600"'),
        ("ON", False, '"ON"'),
        ("a: b", False, '"a: b"'),
        ("a, b", False, "a, b"),
        ("a, b", True, '"a, b"'),
        ("x #y", False, '"x #y"'),
        ("", False, '""'),
        (" padded", False, '" padded"'),
        ("Žluťoučký", False, "Žluťoučký"),
        (1.5, False, "1.5"),
        (1e-07, False, "1.0e-07"),
        (-3, False, "-3"),
        (False, False, "false"),
    ],
)
def test_scalar(value, flow, expected):
    assert scalar(value, flow) == expected
    text = f"{{k: {expected}}}" if flow else f"k: {expected}"
    assert yaml.safe_load(text) == {"k": value}


def test_hex_int_keeps_its_notation():
    value = HexInt("0x3FF")
    assert value == 1023
    assert scalar(value) == "0x3FF"


DOC = {
    "device": {"name": "Dev"},
    "generator": {"outputs": {"reg_map": "../fw"}},
    "blocks": {
        "SYS": {
            "code": 0,
            "name": "SYSTEM",
            "registers": [
                {
                    "name": "STATUS",
                    "type": "BIN",
                    "access": "RO",
                    "size": 4,
                    "description": "Line one\nLine two",
                    "bits": [
                        {"name": "S_ERR", "bit": 0, "label": "Error", "description": "Any, error"},
                        {"name": "S_LONGER_NAME", "bit": 17},
                    ],
                },
                {
                    "name": "MODE",
                    "type": "ENUM",
                    "access": "RWF",
                    "size": 1,
                    "default": "M_B",
                    "modbus": {"address": 100},
                    "values": [{"name": "M_A", "label": "9600"}, {"name": "M_B", "value": 5}],
                },
                {
                    "name": "MAX",
                    "type": "INT",
                    "access": "RW",
                    "size": 2,
                    "max": HexInt("0x3FF"),
                    "modbus": False,
                },
            ],
        }
    },
}


def test_emit_map_layout():
    text = emit_map(DOC)
    assert text.startswith("# Register map Dev\n#\n")
    body = text.split("\ndevice:\n", 1)[1]
    assert body == (
        "  name: Dev\n"
        "\n"
        "generator:\n"
        "  outputs:\n"
        "    reg_map: ../fw\n"
        "\n"
        "blocks:\n"
        "\n"
        "  SYS:\n"
        "    code: 0\n"
        "    name: SYSTEM\n"
        "    registers:\n"
        "      - name: STATUS\n"
        "        type: BIN\n"
        "        access: RO\n"
        "        size: 4\n"
        "        description: |-\n"
        "          Line one\n"
        "          Line two\n"
        "        bits:\n"
        '          - {name: S_ERR,         bit: 0,  label: Error, description: "Any, error"}\n'
        "          - {name: S_LONGER_NAME, bit: 17}\n"
        "\n"
        "      - name: MODE\n"
        "        type: ENUM\n"
        "        access: RWF\n"
        "        size: 1\n"
        "        default: M_B\n"
        "        modbus: {address: 100}\n"
        "        values:\n"
        '          - {name: M_A, label: "9600"}\n'
        "          - {name: M_B, value: 5}\n"
        "\n"
        "      - name: MAX\n"
        "        type: INT\n"
        "        access: RW\n"
        "        size: 2\n"
        "        max: 0x3FF\n"
        "        modbus: false\n"
    )


def test_emitted_map_loads_back():
    reg = load_map_text(emit_map(DOC)).blocks["SYS"].registers
    assert reg[0].description == "Line one\nLine two"
    assert reg[0].bits[0].description == "Any, error"
    assert reg[1].values[0].label == "9600"
    assert (reg[2].max, reg[2].modbus) == (1023, False)


def test_keyword_like_item_names_are_quoted():
    doc = {
        "device": {"name": "Dev"},
        "blocks": {
            "SYS": {
                "code": 0,
                "registers": [
                    {
                        "name": "M",
                        "type": "ENUM",
                        "access": "RW",
                        "size": 1,
                        "values": [{"name": "ON"}, {"name": "NULL", "label": "x"}],
                    }
                ],
            }
        },
    }
    values = load_map_text(emit_map(doc)).blocks["SYS"].registers[0].values
    assert [v.name for v in values] == ["ON", "NULL"]
