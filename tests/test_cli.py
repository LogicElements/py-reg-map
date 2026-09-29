import json

import pytest
from helpers import map_yaml

from regmap import __version__
from regmap.cli import main

BLOCKS = "SYS: {code: 0, registers: [{name: A, type: INT, access: RW, size: 2}]}"


@pytest.fixture
def map_file(tmp_path):
    path = tmp_path / "dev.yaml"
    path.write_text(map_yaml(BLOCKS), encoding="utf-8")
    return path


def test_generate_writes_then_reports_unchanged(map_file, capsys):
    assert main(["generate", str(map_file)]) == 0
    assert capsys.readouterr().out.count("written") == 7
    assert (map_file.parent / "reg_map.h").is_file()
    assert main(["generate", str(map_file)]) == 0
    assert capsys.readouterr().out.count("unchanged") == 7


def test_check_mode(map_file, capsys):
    assert main(["generate", str(map_file), "--check"]) == 1
    assert capsys.readouterr().out.count("outdated") == 7
    assert not (map_file.parent / "reg_map.h").exists()
    main(["generate", str(map_file)])
    assert main(["generate", str(map_file), "--check"]) == 0


def test_out_option(map_file, tmp_path):
    assert main(["generate", str(map_file), "--out", str(tmp_path / "gen")]) == 0
    assert (tmp_path / "gen" / "Dev_Modbus.json").is_file()


def test_breaking_change_needs_confirmation(map_file, capsys):
    main(["generate", str(map_file)])
    before = (map_file.parent / "reg_map.h").read_bytes()
    map_file.write_text(
        map_yaml(
            BLOCKS.replace("[{name: A", "[{name: NEW, type: INT, access: RW, size: 2}, {name: A")
        )
    )
    capsys.readouterr()
    assert main(["generate", str(map_file)]) == 1
    err = capsys.readouterr().err
    assert "breaking changes against the previous outputs, nothing written" in err
    assert "SYS_A  Id: 0x00000151 -> 0x00002151" in err
    assert (map_file.parent / "reg_map.h").read_bytes() == before
    assert main(["generate", str(map_file), "--allow-id-change"]) == 0
    assert "warning: breaking changes accepted" in capsys.readouterr().err
    assert (map_file.parent / "reg_map.h").read_bytes() != before


def test_corrupt_previous_output_needs_confirmation(map_file, capsys):
    (map_file.parent / "Dev_Modbus.json").write_text("garbage")
    assert main(["generate", str(map_file)]) == 1
    assert "use --allow-id-change to overwrite it" in capsys.readouterr().err
    assert main(["generate", str(map_file), "--allow-id-change"]) == 0


def test_invalid_map_reports_all_problems(tmp_path, capsys):
    path = tmp_path / "bad.yaml"
    path.write_text(
        map_yaml("SYS: {code: 0, registers: [{name: A, type: INT, access: RW, size: 3, x: 1}]}")
    )
    assert main(["generate", str(path)]) == 1
    err = capsys.readouterr().err
    assert f"error: {path}: 2 problem(s)" in err
    assert "blocks.SYS.registers[0] (A).size: size 3 is not allowed for INT" in err
    assert "blocks.SYS.registers[0] (A).x: unknown key" in err


def test_template_without_placeholder_fails(tmp_path, capsys):
    (tmp_path / "tpl").mkdir()
    (tmp_path / "tpl" / "reg_map_temp.h").write_text("nothing here\n")
    path = tmp_path / "dev.yaml"
    path.write_text(map_yaml(BLOCKS, extra="generator: {templates: tpl}\n"))
    assert main(["generate", str(path)]) == 1
    assert (
        "reg_map_temp.h: placeholder /* < DEFINE REG MAP > */ not found" in capsys.readouterr().err
    )


def test_missing_file_and_bad_arguments_exit_2(tmp_path, capsys):
    assert main(["generate", str(tmp_path / "missing.yaml")]) == 2
    with pytest.raises(SystemExit) as exc:
        main(["generate"])
    assert exc.value.code == 2


def test_schema(tmp_path, capsys):
    assert main(["schema"]) == 0
    schema = json.loads(capsys.readouterr().out)
    assert set(schema["properties"]) == {"device", "generator", "blocks"}
    assert main(["schema", "-o", str(tmp_path / "s.json")]) == 0
    assert json.loads((tmp_path / "s.json").read_text()) == schema


def test_version(capsys):
    with pytest.raises(SystemExit):
        main(["--version"])
    assert capsys.readouterr().out.strip() == f"regmap {__version__}"


def test_relative_map_path_writes_next_to_the_map(tmp_path, monkeypatch):
    project = tmp_path / "project"
    project.mkdir()
    (project / "dev.yaml").write_text(
        map_yaml(BLOCKS, extra="generator: {outputs: {python: py}}\n")
    )
    elsewhere = tmp_path / "elsewhere"
    elsewhere.mkdir()
    monkeypatch.chdir(elsewhere)
    assert main(["generate", "../project/dev.yaml"]) == 0
    assert (project / "reg_map.h").is_file()
    assert (project / "py" / "DevRegs.py").is_file()
    assert list(elsewhere.iterdir()) == []


def test_non_utf8_map_is_a_clean_error(tmp_path, capsys):
    path = tmp_path / "dev.yaml"
    path.write_bytes(map_yaml(BLOCKS, extra="# Žluťoučký\n").encode("cp1250"))
    assert main(["generate", str(path)]) == 1
    assert capsys.readouterr().err.startswith("error: ")


def test_unwritable_destination_is_a_clean_error(tmp_path, capsys):
    (tmp_path / "taken").write_text("a file, not a directory")
    path = tmp_path / "dev.yaml"
    path.write_text(map_yaml(BLOCKS, extra="generator: {outputs: {reg_map: taken}}\n"))
    assert main(["generate", str(path)]) == 1
    assert capsys.readouterr().err.startswith("error: ")


def test_missing_templates_directory_is_an_error(tmp_path, capsys):
    path = tmp_path / "dev.yaml"
    path.write_text(map_yaml(BLOCKS, extra="generator: {templates: nodir}\n"))
    assert main(["generate", str(path)]) == 1
    assert "templates directory" in capsys.readouterr().err
    assert not (tmp_path / "reg_map.h").exists()
