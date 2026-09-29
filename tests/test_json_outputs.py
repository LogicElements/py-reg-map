import json

from helpers import resolved

from regmap.generators import lebin_json, modbus_json

MAP = """
SYS:
  code: 0
  registers:
    - name: VERSION
      type: INT
      access: ROF
      size: 4
      label: Version
      default: 1001
      min: 1001
      max: 0x3FF0
      unit: "-"
      description: |-
        Line one
        Line two
    - {name: STATUS, type: BIN, access: RO, size: 4, label: Status, description: Flags, bits: [{name: S_ERR, bit: 0, label: Error, description: Any error}, {name: S_WD, bit: 17, label: Watchdog}]}
    - {name: RAW, type: BIN, access: RO, size: 2}
    - {name: MODE, type: ENUM, access: RWIF, size: 1, default: M_B, values: [{name: M_A, label: A, description: First}, {name: M_B, value: 4}]}
    - {name: TEMP, type: FLOAT, access: RW, size: 4, default: 21.5, modbus: {format: F32}}
    - {name: TEXT, type: STRING, access: RW, size: 8, default: hi}
    - {name: HIDDEN, type: INT, access: RW, size: 2, modbus: false}
"""


def lebin() -> dict[str, dict]:
    return {e["Name"]: e for e in json.loads(lebin_json.render(resolved(MAP)))}


def modbus() -> dict[str, dict]:
    return {e["Name"]: e for e in json.loads(modbus_json.render(resolved(MAP)))}


def test_lebin_plain_register():
    assert lebin()["VERSION"] == {
        "Category": "SYS",
        "Name": "VERSION",
        "Label": "Version",
        "Id": 0x00000132,
        "VarType": "INT",
        "Access": "ROF",
        "Description": "Line one\r\nLine two",
        "Value": "1001",
        "NewValue": "1001",
        "Reset": "1001",
        "Min": 1001,
        "Max": 0x3FF0,
        "Units": "-",
    }
    assert list(lebin()["VERSION"]) == [
        "Category", "Name", "Label", "Id", "VarType", "Access", "Description",
        "Value", "NewValue", "Reset", "Min", "Max", "Units",
    ]  # fmt: skip


def test_lebin_bits_enum_and_access():
    entries = lebin()
    status = entries["STATUS"]
    assert status["Description"] == (
        "Flags\r\nMeaning of respective bits: \r\n[0] - Error - Any error.\r\n[17] - Watchdog.\r\n"
    )
    assert (status["EnumStr"], status["EnumValue"], status["Value"]) == (
        ["Error", "Watchdog"],
        [0, 17],
        "",
    )
    raw = entries["RAW"]
    assert (raw["EnumStr"], raw["EnumValue"], raw["Description"]) == ([], [], "")
    mode = entries["MODE"]
    assert mode["Access"] == "RWF"
    assert (mode["Value"], mode["EnumStr"], mode["EnumValue"]) == ("4", ["A", "M_B"], [0, 4])
    assert mode["Description"] == "Allowed values: \r\nA - First.\r\nM_B.\r\n"
    assert "Min" not in mode and "Max" not in mode
    assert list(mode)[8:12] == ["NewValue", "EnumStr", "EnumValue", "Reset"]
    assert entries["TEMP"]["Value"] == "21.5"
    assert entries["TEXT"]["Value"] == "hi"
    assert "IsVisible" not in mode and "ConfigUser" not in mode
    assert len(entries) == 7


def test_modbus_entries():
    entries = modbus()
    assert "SYS_HIDDEN" not in entries
    assert entries["SYS_VERSION"] == {
        "Type": "INPUT",
        "Name": "SYS_VERSION",
        "Id": 0x00000132,
        "Address": [0, 1],
        "Format": "INT",
        "Value": 1001,
        "Access": "ROF",
        "Min": 1001,
        "Max": 0x3FF0,
        "Unit": "-",
        "Label": "Version",
        "Description": "Line one\r\nLine two",
    }
    status = entries["SYS_STATUS"]
    assert (status["Min"], status["Max"], status["Value"]) == (0, 0, 0)
    assert status["Description"].endswith("Bit 0 - Error - Any error.\r\nBit 17 - Watchdog.\r\n")
    mode = entries["SYS_MODE"]
    assert (mode["Type"], mode["Access"], mode["Min"], mode["Max"], mode["Value"]) == (
        "HOLD",
        "RWIF",
        0,
        4,
        4,
    )
    assert mode["Description"] == "Allowed values: \r\nValue 0 - A - First.\r\nValue 4 - M_B.\r\n"
    assert list(mode)[-3:] == ["EnumStr", "EnumValue", "Description"]
    assert (entries["SYS_TEMP"]["Format"], entries["SYS_TEMP"]["Address"]) == ("FLOAT32", [1, 2])
    assert entries["SYS_TEXT"]["Value"] == "hi"


def test_json_text_layout():
    text = lebin_json.render(resolved(MAP))
    assert text.startswith('[\n  {\n    "Category": "SYS",\n')
    assert text.endswith("}\n]\n")
