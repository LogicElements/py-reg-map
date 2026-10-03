import os
from pathlib import Path

from helpers import load

from regmap.outputs import render_outputs
from regmap.resolve import resolve
from regmap.writer import encode, stale, write

BLOCKS = "SYS: {code: 0, registers: [{name: A, type: INT, access: RO, size: 2, label: A}]}"
DESTINATIONS = """\
generator:
  templates: tpl
  outputs:
    reg_map: fw/common
    modbus: fw/serial
    lebin_json: tests
"""


def outputs(base: Path, out: Path | None = None, extra: str = ""):
    rmap = load(BLOCKS, extra=extra)
    return render_outputs(resolve(rmap), rmap.generator, base, out)


def test_file_names_and_default_destination(tmp_path):
    files = outputs(tmp_path)
    assert [(o.key, o.path.relative_to(tmp_path).as_posix()) for o in files] == [
        ("reg_map", "reg_map.h"),
        ("reg_map", "reg_map.c"),
        ("modbus", "mb_rtu_app.h"),
        ("modbus", "mb_rtu_app.c"),
        ("lebin_json", "Dev_registers.json"),
        ("modbus_json", "Dev_Modbus.json"),
        ("python", "DevRegs.py"),
        ("html", "Dev_registers.html"),
    ]
    assert "#define CONF_SYS_A " in files[0].text


def test_configured_destinations_and_project_templates(tmp_path):
    (tmp_path / "tpl").mkdir()
    (tmp_path / "tpl" / "reg_map_temp.c").write_text(
        "// project\n/* < DEFINE REG MAP STORAGE > */\n/* < REG MAP FACTORY > */\n"
    )
    files = {
        o.path.relative_to(tmp_path).as_posix(): o for o in outputs(tmp_path, extra=DESTINATIONS)
    }
    assert sorted(files) == [
        "DevRegs.py",
        "Dev_Modbus.json",
        "Dev_registers.html",
        "fw/common/reg_map.c",
        "fw/common/reg_map.h",
        "fw/serial/mb_rtu_app.c",
        "fw/serial/mb_rtu_app.h",
        "tests/Dev_registers.json",
    ]
    assert files["fw/common/reg_map.c"].text.startswith("// project\nconf_reg_t conf;\n")


def test_html_output_can_be_disabled(tmp_path):
    files = outputs(tmp_path, extra="generator:\n  outputs:\n    html: false\n")
    assert "html" not in {o.key for o in files}


def test_out_dir_overrides_configured_destinations(tmp_path):
    files = outputs(
        tmp_path, out=tmp_path / "gen", extra=DESTINATIONS.replace("  templates: tpl\n", "")
    )
    assert {o.path.parent for o in files} == {tmp_path / "gen"}


def test_writer_uses_crlf_and_skips_unchanged_files(tmp_path):
    files = outputs(tmp_path)
    assert encode("a\nb\n") == b"a\r\nb\r\n"
    assert stale(files) == [o.path for o in files]
    assert {status for _, status in write(files)} == {"written"}
    assert b"\r\n" in files[0].path.read_bytes() and b"\r\r" not in files[0].path.read_bytes()
    os.utime(files[0].path, (1_000_000, 1_000_000))
    assert {status for _, status in write(files)} == {"unchanged"}
    assert files[0].path.stat().st_mtime == 1_000_000
    assert stale(files) == []


def test_writer_creates_missing_destination_directories(tmp_path):
    files = outputs(tmp_path, extra="generator: {outputs: {reg_map: deep/new/dir}}\n")
    write(files)
    assert (tmp_path / "deep" / "new" / "dir" / "reg_map.h").is_file()
