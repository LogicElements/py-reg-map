import json

import pytest
from helpers import load

from regmap.check import (
    Change,
    PreviousOutputError,
    breaking_changes,
    compare_lebin,
    compare_modbus,
    format_changes,
)
from regmap.outputs import render_outputs
from regmap.resolve import resolve

BASE = """
SYS:
  code: 0
  registers:
    - {name: A, type: INT, access: RW, size: 2}
    - {name: B, type: INT, access: RW, size: 2}
"""


def render(blocks: str, base):
    rmap = load(blocks)
    return render_outputs(resolve(rmap), rmap.generator, base)


def write_previous(files) -> None:
    for o in files:
        o.path.write_text(o.text, encoding="utf-8")


def test_first_generation_has_nothing_to_compare(tmp_path):
    assert breaking_changes(render(BASE, tmp_path)) == []


def test_same_map_has_no_changes(tmp_path):
    write_previous(render(BASE, tmp_path))
    assert breaking_changes(render(BASE, tmp_path)) == []


def test_inserted_register_shifts_id_and_modbus_address(tmp_path):
    write_previous(render(BASE, tmp_path))
    inserted = BASE.replace(
        "    - {name: B", "    - {name: NEW, type: INT, access: RW, size: 2}\n    - {name: B"
    )
    assert breaking_changes(render(inserted, tmp_path)) == [
        Change("SYS_B", "Id", "0x00002151", "0x00004151"),
        Change("SYS_B", "Modbus", "HOLD [1]", "HOLD [2]"),
    ]


def test_removed_register_and_removed_from_modbus(tmp_path):
    write_previous(render(BASE, tmp_path))
    changed = BASE.replace("    - {name: B, type: INT, access: RW, size: 2}\n", "").replace(
        "size: 2}", "size: 2, modbus: false}"
    )
    assert breaking_changes(render(changed, tmp_path)) == [
        Change("SYS_B", "register", "present", "removed"),
        Change("SYS_A", "Modbus", "HOLD [0]", "not exposed"),
        Change("SYS_B", "Modbus", "HOLD [1]", "not exposed"),
    ]


def test_legacy_cp1250_lebin_file_is_compared(tmp_path):
    files = render(BASE, tmp_path)
    legacy = [{"Category": "SYS", "Name": "A", "Id": 1, "Description": "Hodnota …"}]
    files[4].path.write_bytes(json.dumps(legacy, ensure_ascii=False).encode("cp1250"))
    assert breaking_changes(files) == [Change("SYS_A", "Id", "0x00000001", "0x00000151")]


def test_unreadable_previous_file_is_reported(tmp_path):
    files = render(BASE, tmp_path)
    files[5].path.write_text("{ not json", encoding="utf-8")
    with pytest.raises(PreviousOutputError, match="cannot read previous file"):
        breaking_changes(files)


def test_entries_without_names_are_ignored():
    assert compare_lebin([{"Id": 5}], []) == []
    assert compare_modbus([{"Type": "HOLD"}], []) == []


def test_format_changes_aligns_register_names():
    text = format_changes(
        [Change("SYS_A", "Id", "0x1", "0x2"), Change("SYS_LONG", "Modbus", "HOLD [1]", "HOLD [2]")]
    )
    assert text == "  SYS_A     Id: 0x1 -> 0x2\n  SYS_LONG  Modbus: HOLD [1] -> HOLD [2]"
