"""VMS-1511: outputs generated from example/vms1511.yaml against the VBA reference files."""

import json

import pytest
from helpers import EXAMPLE

from regmap.generators import lebin_json, modbus_json
from regmap.model import load_map
from regmap.outputs import render_outputs
from regmap.resolve import resolve
from regmap.writer import encode

LEGEND_HEADERS = ("Allowed values: \r\n", "Meaning of respective bits: \r\n")


@pytest.fixture(scope="module")
def generated(tmp_path_factory):
    rmap = load_map(EXAMPLE / "vms1511.yaml")
    out = tmp_path_factory.mktemp("vms1511")
    return {o.path.name: o for o in render_outputs(resolve(rmap), rmap.generator, EXAMPLE, out)}


@pytest.mark.parametrize("name", ["reg_map.h", "reg_map.c", "mb_rtu_app.h", "mb_rtu_app.c"])
def test_c_files_are_byte_identical(generated, name):
    assert encode(generated[name].text) == (EXAMPLE / name).read_bytes()


def test_python_file_is_identical_except_bom(generated):
    reference = (EXAMPLE / "Vms1511Regs.py").read_bytes().removeprefix(b"\xef\xbb\xbf")
    assert encode(generated["Vms1511Regs.py"].text) == reference


def apply_documented_differences(description: str) -> str:
    """doc/differences-from-vba.md items 2 and 15 applied to a VBA description."""
    text, legend = description, ""
    for header in LEGEND_HEADERS:
        if header in description:
            text, legend = description.split(header, 1)
            legend = header + legend.replace(" - .\r\n", ".\r\n")
            text = text.removesuffix("\r\n")
    text = "\r\n".join(line.rstrip() for line in text.split("\r\n"))
    return (text + "\r\n" if text and legend else text) + legend


@pytest.mark.parametrize(
    ("name", "render", "encoding"),
    [
        ("Vms1511_registers.json", lebin_json.render, "cp1250"),
        ("Vms1511_Modbus.json", modbus_json.render, "utf-8-sig"),
    ],
)
def test_json_files_match_after_documented_differences(name, render, encoding):
    reference = json.loads((EXAMPLE / name).read_bytes().decode(encoding))
    for entry in reference:
        entry.pop("IsVisible", None)  # difference 1
        entry.pop("ConfigUser", None)
        entry["Description"] = apply_documented_differences(entry["Description"])
    generated = json.loads(render(resolve(load_map(EXAMPLE / "vms1511.yaml"))))
    assert generated == reference
    assert [list(e) for e in generated] == [list(e) for e in reference]  # key order


def test_import_reproduces_the_committed_yaml():
    pytest.importorskip("openpyxl")
    from regmap.importers.xlsx import import_workbook

    result = import_workbook(EXAMPLE / "Vms1511.xlsm", EXAMPLE / "vms1511.yaml")
    assert result.warnings == []
    assert encode(result.yaml_text) == (EXAMPLE / "vms1511.yaml").read_bytes()
