from pathlib import Path

import pytest

openpyxl = pytest.importorskip("openpyxl")

from regmap.cli import main  # noqa: E402
from regmap.codes import Access, RegType, register_id  # noqa: E402
from regmap.importers.xlsx import (  # noqa: E402
    ImportFailed,
    _destination,
    _number,
    _text,
    _vba_word_count,
    import_workbook,
)
from regmap.model import load_map_text  # noqa: E402

HEADER = [
    "Block", "Name source", "Block Code", "Data Type", "Access Code", "Address", "Max length",
    "ID [hex]", "Name", "Label", "Factory\nvalue", "Minimal value", "Maximal value", "Units",
    "Modbus special", "Invalid", "Description",
]  # fmt: skip
CODES = {"SYS": 0, "COM": 3}


def reg(name, block, rtype, access, address, size, **cols):
    """A register row; the ID column is computed unless ``id`` is given."""
    rid = cols.pop("id", None)
    if rid is None:
        rid = f"0x{register_id(CODES[block], address, RegType(rtype), Access(access), size):08X}"
    return {
        "Name source": name, "Block Code": block, "Data Type": rtype, "Access Code": access,
        "Address": address, "Max length": size, "ID [hex]": rid, "Name": f"{block}_{name}",
        "Label": cols.get("label"), "Factory\nvalue": cols.get("default"),
        "Minimal value": cols.get("min"), "Maximal value": cols.get("max"),
        "Units": cols.get("unit"), "Modbus special": cols.get("modbus"),
        "Invalid": cols.get("invalid"), "Description": cols.get("description"),
    }  # fmt: skip


def item(name, label=None, override=None, description=None):
    return {"Name": name, "Label": label, "Factory\nvalue": override, "Description": description}


def make_workbook(path: Path, rows: list[dict], top: list[list] | None = None) -> Path:
    wb = openpyxl.Workbook()
    ws = wb.active
    ws.title = "Registers"
    for row in top or []:
        ws.append(row)
    ws.append([None, None, None, *HEADER])
    for row in rows:
        ws.append([None, None, None, None, *[row.get(h) for h in HEADER[1:]]])
    common = wb.create_sheet("Common")
    common.append([None, None, "Block ID"])
    common.append([None, None, "Abbrev.", "Code", "Name", "Description"])
    for abbrev, code in CODES.items():
        common.append([None, None, abbrev, code, f"{abbrev} name", f"{abbrev} description"])
    common.append([None, None, None, 7])
    wb.save(path)
    return path


def run(tmp_path: Path, rows: list[dict], top: list[list] | None = None):
    book = make_workbook(tmp_path / "Dev.xlsx", rows, top)
    result = import_workbook(book, tmp_path / "dev.yaml")
    return result, load_map_text(result.yaml_text)


def test_registers_items_and_addresses(tmp_path):
    rows = [
        reg("UPTIME", "SYS", "INT", "RO", 0, 4, label="Uptime", default=0, unit="s"),
        reg("STATUS", "SYS", "BIN", "RO", 4, 4, description="Flags  \nsecond line "),
        item("S_A", "A"),
        item("S_B", "B", override=16),
        item("S_C", "C"),
        reg("MODE", "SYS", "ENUM", "RWF", 12, 1, default=2, min=0, max=3),
        item("M_A"),
        item("M_B", override=2, description="two"),
        item("M_C"),
        {"Data Type": "stop"},
        reg("LIMIT", "SYS", "INT", "RW", 14, 2, max="0x3FF", default="0x10"),
    ]
    result, rmap = run(tmp_path, rows)
    assert result.warnings == []
    assert result.register_count == 4
    uptime, status, mode, limit = rmap.blocks["SYS"].registers
    assert (uptime.label, uptime.default, uptime.unit, uptime.address) == ("Uptime", 0, "s", None)
    assert status.description == "Flags\nsecond line"
    assert [(b.name, b.bit) for b in status.bits] == [("S_A", 0), ("S_B", 16), ("S_C", 17)]
    assert mode.address == 12  # gap after STATUS
    assert [(v.name, v.value) for v in mode.values] == [("M_A", None), ("M_B", 2), ("M_C", None)]
    assert (mode.default, mode.min, mode.max) == ("M_B", None, None)
    assert (limit.max, limit.default) == (1023, 16)
    assert "max: 0x3FF" in result.yaml_text
    assert rmap.blocks["SYS"].name == "SYS name"


def test_invalid_rows_and_stray_items_are_skipped_with_warnings(tmp_path):
    rows = [
        reg("OLD", "SYS", "ENUM", "RW", 0, 1, invalid="x"),
        item("OLD_A"),
        reg("A", "SYS", "INT", "RW", 1, 1),
        item("STRAY"),
    ]
    result, rmap = run(tmp_path, rows)
    assert [r.name for r in rmap.blocks["SYS"].registers] == ["A"]
    assert result.warnings == [
        "row 2: OLD is marked Invalid, skipped",
        "row 5: item STRAY under INT register ignored",
    ]


def test_modbus_specials_follow_vba_allocation(tmp_path):
    rows = [
        reg("I1", "SYS", "INT", "RO", 0, 2, modbus="HA 50"),  # moves the HOLD counter only
        reg("H1", "SYS", "INT", "RW", 2, 2),
        reg("H2", "SYS", "INT", "RW", 4, 2, modbus="HR 3"),  # skip 3 holding registers
        reg("X", "SYS", "INT", "RW", 6, 2, modbus="x"),
        reg("F", "SYS", "FLOAT", "RW", 8, 4, modbus="F32"),
        reg("Q", "SYS", "INT", "RW", 12, 2, modbus="??"),
    ]
    result, rmap = run(tmp_path, rows)
    modbus = {r.name: r.modbus for r in rmap.blocks["SYS"].registers}
    assert modbus["I1"] is None
    assert modbus["H1"].address == 50
    assert modbus["H2"].address == 54
    assert modbus["X"] is False
    assert (modbus["F"].address, modbus["F"].format) == (None, "F32")
    assert result.warnings == ["row 7: unknown Modbus special '??' ignored"]


def test_id_mismatch_fails(tmp_path):
    book = make_workbook(
        tmp_path / "Dev.xlsx", [reg("A", "SYS", "INT", "RO", 0, 4, id="0x00000999")]
    )
    with pytest.raises(ImportFailed) as exc:
        import_workbook(book, tmp_path / "dev.yaml")
    assert exc.value.errors == [
        "SYS_A: computed ID 0x00000112 differs from the sheet ID 0x00000999"
    ]


def test_unknown_block_fails(tmp_path):
    row = reg("A", "SYS", "INT", "RO", 0, 4) | {"Block Code": "ZZZ"}
    book = make_workbook(tmp_path / "Dev.xlsx", [row])
    with pytest.raises(ImportFailed, match="row 2: block 'ZZZ' is not in the Common sheet"):
        import_workbook(book, tmp_path / "dev.yaml")


def test_missing_column_fails(tmp_path):
    book = make_workbook(tmp_path / "Dev.xlsx", [reg("A", "SYS", "INT", "RO", 0, 4)])
    wb = openpyxl.load_workbook(book)
    wb["Registers"]["T1"] = None  # the Description header
    wb.save(book)
    with pytest.raises(ImportFailed, match="Registers sheet: missing column"):
        import_workbook(book, tmp_path / "dev.yaml")


def test_config_cells_and_destinations(tmp_path):
    top = [
        [None, None, None, "Config storage", None, "MY_REG"],
        [None, None, None, "RegMap destination", None, '=GetPath()&"\\..\\Firmware\\Core\\"'],
        [None, None, None, "Tests destination", None, "C:\\Users\\someone\\Tests\\"],
    ]
    result, rmap = run(tmp_path, [reg("A", "SYS", "INT", "RO", 0, 4)], top)
    assert rmap.device.c_storage == "MY_REG"
    assert rmap.device.c_prefix == "CONF_"
    assert rmap.generator.outputs.reg_map == "../Firmware/Core"
    assert rmap.generator.outputs.python is None
    assert result.warnings == ["'tests destination' is not a GetPath() formula, not imported"]


@pytest.mark.parametrize(
    ("cell", "expected"),
    [
        (None, None),
        ("", None),
        (4, 4),
        (4.0, 4),
        (1.5, 1.5),
        ("12", 12),
        ("1,5", 1.5),
        ("abc", None),
    ],
)
def test_number(cell, expected):
    assert _number(cell) == expected


def test_number_keeps_hex_text():
    assert _number(" 0x3FF ").text == "0x3FF"


def test_text():
    assert _text(None) is None
    assert _text("  ") is None
    assert _text(9600.0) == "9600"
    assert _text("a  \r\nb \n\n") == "a\nb"


@pytest.mark.parametrize(
    ("reg_type", "size", "f32", "words"),
    [("INT", 1, False, 1), ("INT", 4, False, 2), ("STRING", 3, False, 2), ("STRING", 5, False, 2),
     ("ENUM", 1, False, 1), ("FLOAT", 4, False, 1), ("FLOAT", 4, True, 2)],
)  # fmt: skip
def test_vba_word_count_uses_bankers_rounding(reg_type, size, f32, words):
    assert _vba_word_count(reg_type, size, f32) == words


def test_destination_formula(tmp_path):
    book_dir = tmp_path / "Documents"
    assert _destination('=GetPath()&"\\..\\Firmware\\"', book_dir, book_dir) == "../Firmware"
    assert _destination('=GetPath()&"\\..\\Firmware\\"', book_dir, tmp_path) == "Firmware"
    assert _destination("C:\\abs\\path", book_dir, book_dir) is None


def test_cli_import_refuses_to_overwrite(tmp_path, capsys):
    book = make_workbook(tmp_path / "Dev.xlsx", [reg("A", "SYS", "INT", "RO", 0, 4)])
    assert main(["import-xlsx", str(book)]) == 0
    assert (tmp_path / "dev.yaml").is_file()
    assert "written" in capsys.readouterr().out
    assert main(["import-xlsx", str(book)]) == 1
    assert "already exists; use --force" in capsys.readouterr().err
    assert main(["import-xlsx", str(book), "--force", "-o", str(tmp_path / "other.yaml")]) == 0
    assert main(["import-xlsx", str(tmp_path / "missing.xlsx")]) == 2


def test_items_after_a_stop_row_are_not_attached(tmp_path):
    rows = [
        reg("MODE", "SYS", "ENUM", "RW", 0, 1),
        item("M_A"),
        {"Data Type": "stop"},
        item("LOST"),
    ]
    result, rmap = run(tmp_path, rows)
    assert [v.name for v in rmap.blocks["SYS"].registers[0].values] == ["M_A"]
    assert result.warnings == ["row 5: item LOST has no register above it, ignored"]


def test_not_an_excel_file_is_a_clean_error(tmp_path, capsys):
    bogus = tmp_path / "Dev.xlsx"
    bogus.write_text("not a zip")
    assert main(["import-xlsx", str(bogus)]) == 1
    assert "import failed" in capsys.readouterr().err


def test_enum_and_bit_names_that_look_like_yaml_keywords(tmp_path):
    rows = [
        reg("MODE", "SYS", "ENUM", "RW", 0, 1),
        item("ON"),
        item("NULL"),
        reg("FLAGS", "SYS", "BIN", "RO", 1, 1),
        item("OFF"),
    ]
    _, rmap = run(tmp_path, rows)
    mode, flags = rmap.blocks["SYS"].registers
    assert [v.name for v in mode.values] == ["ON", "NULL"]
    assert [b.name for b in flags.bits] == ["OFF"]
