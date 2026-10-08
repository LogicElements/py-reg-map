import pytest

from regmap import __version__
from regmap.cli import main
from regmap.export_lib import (
    MARKER,
    ExportError,
    export,
    is_pristine,
    library_files,
    stamp,
)
from regmap.writer import encode


def test_library_has_core_and_port_files():
    kinds = {f.name: f.core for f in library_files()}
    assert len(kinds) == len(library_files())  # no duplicate names
    assert kinds["lib_bytes.h"] is True
    assert kinds["common.h"] is False
    assert kinds["regmap_lib_conf.h"] is False


def test_core_files_are_stamped_and_port_files_are_not():
    for f in library_files():
        if f.core:
            assert f"{MARKER} {__version__} sha256:" in f.text, f.name
            assert is_pristine(f.text), f.name
        else:
            assert MARKER not in f.text, f.name


def test_is_pristine_detects_edits():
    text = stamp("/*\n * @regmap-lib\n */\nint a;\n")
    assert is_pristine(text)
    assert is_pristine(text.replace("\n", "\r\n"))  # line endings do not matter
    assert not is_pristine(text.replace("int a;", "int b;"))
    assert not is_pristine("int a;\n")  # no marker at all


def test_first_export_writes_everything_with_crlf(tmp_path):
    target = tmp_path / "lib"
    report = export(target)
    assert len(report) == len(library_files())
    assert {status for _, status in report} == {"written"}
    data = (target / "common.h").read_bytes()
    assert b"\r\n" in data and b"\n" not in data.replace(b"\r\n", b"")


def test_reexport_reports_unchanged(tmp_path):
    export(tmp_path)
    assert {status for _, status in export(tmp_path)} == {"unchanged"}


def test_reexport_keeps_edited_port_file(tmp_path):
    export(tmp_path)
    conf = tmp_path / "regmap_lib_conf.h"
    conf.write_bytes(b"/* project settings */\r\n")
    report = dict(export(tmp_path))
    assert report[conf] == "kept"
    assert conf.read_bytes() == b"/* project settings */\r\n"


def test_force_ports_overwrites_port_file(tmp_path):
    export(tmp_path)
    conf = tmp_path / "regmap_lib_conf.h"
    conf.write_bytes(b"/* project settings */\r\n")
    report = dict(export(tmp_path, force_ports=True))
    assert report[conf] == "updated"
    original = next(f for f in library_files() if f.name == "regmap_lib_conf.h")
    assert conf.read_bytes() == encode(original.text)


def test_reexport_updates_unmodified_older_core_file(tmp_path):
    export(tmp_path)
    core = tmp_path / "lib_bytes.h"
    core.write_bytes(encode(stamp("/*\n * @regmap-lib\n */\n/* older library */\n")))
    report = dict(export(tmp_path))
    assert report[core] == "updated"
    current = next(f for f in library_files() if f.name == "lib_bytes.h")
    assert core.read_bytes() == encode(current.text)


def test_edited_core_file_stops_the_whole_export(tmp_path):
    export(tmp_path)
    core = tmp_path / "lib_bytes.h"
    core.write_bytes(core.read_bytes() + b"/* local change */\r\n")
    (tmp_path / "common.h").unlink()
    with pytest.raises(ExportError) as exc:
        export(tmp_path)
    assert exc.value.paths == [core]
    assert not (tmp_path / "common.h").exists()  # nothing written


def test_force_overwrites_edited_core_file(tmp_path):
    export(tmp_path)
    core = tmp_path / "lib_bytes.h"
    core.write_bytes(b"/* local change */\r\n")
    report = dict(export(tmp_path, force=True))
    assert report[core] == "updated"


def test_cli_export_lib(tmp_path, capsys):
    assert main(["export-lib", str(tmp_path / "fw")]) == 0
    assert capsys.readouterr().out.count("written") == len(library_files())
    assert main(["export-lib", str(tmp_path / "fw")]) == 0
    assert capsys.readouterr().out.count("unchanged") == len(library_files())


def test_cli_export_lib_refuses_edited_core(tmp_path, capsys):
    main(["export-lib", str(tmp_path)])
    core = tmp_path / "lib_bytes.h"
    core.write_bytes(b"/* local change */\r\n")
    capsys.readouterr()
    assert main(["export-lib", str(tmp_path)]) == 1
    err = capsys.readouterr().err
    assert "error: 1 library file(s) were edited by hand, nothing written" in err
    assert str(core) in err
    assert "--force" in err
    assert main(["export-lib", str(tmp_path), "--force"]) == 0


def test_cli_export_lib_target_is_a_file(tmp_path, capsys):
    target = tmp_path / "file"
    target.write_text("x")
    assert main(["export-lib", str(target)]) == 1
    assert capsys.readouterr().err.startswith("error: ")
