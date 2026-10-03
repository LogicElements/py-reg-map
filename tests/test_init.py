import json

import pytest
from helpers import EXAMPLE

from regmap.cli import main
from regmap.model import load_map_text
from regmap.resolve import resolve
from regmap.scaffold import TEMPLATE, new_map_text, strip_generator
from regmap.templating import load_template


def test_new_map_is_valid_and_named():
    text = new_map_text("Acme42")
    rmap = load_map_text(text)
    assert rmap.device.name == "Acme42"
    assert resolve(rmap).name == "Acme42"
    assert "Template" not in text and "1511" not in text


def test_new_map_lists_every_output_next_to_the_yaml():
    outputs = load_map_text(new_map_text("Dev")).generator.outputs
    assert outputs.model_dump() == dict.fromkeys(type(outputs).model_fields, ".")


def test_cli_init_writes_and_refuses_to_overwrite(tmp_path, capsys):
    target = tmp_path / "dev.yaml"
    assert main(["init", "Dev", "-o", str(target)]) == 0
    assert b"\r\n" in target.read_bytes()
    assert main(["init", "Dev", "-o", str(target)]) == 1
    assert "--force" in capsys.readouterr().err
    assert main(["init", "Dev", "-o", str(target), "--force"]) == 0


def test_cli_init_rejects_a_bad_name(tmp_path, capsys):
    assert main(["init", "1bad", "-o", str(tmp_path / "x.yaml")]) == 1
    assert not (tmp_path / "x.yaml").exists()
    assert "device name" in capsys.readouterr().err


def test_init_writes_the_schema_next_to_the_map(tmp_path):
    target = tmp_path / "sub" / "dev.yaml"
    target.parent.mkdir()
    assert main(["init", "Dev", "-o", str(target)]) == 0
    schema = target.parent / "regmap.schema.json"
    assert json.loads(schema.read_text(encoding="utf-8"))["title"] == "RegisterMap"
    first = target.read_text(encoding="utf-8").splitlines()[0]
    assert first == "# yaml-language-server: $schema=regmap.schema.json"


def test_generated_init_map_generates_outputs(tmp_path):
    main(["init", "Dev", "-o", str(tmp_path / "dev.yaml")])
    assert main(["generate", str(tmp_path / "dev.yaml")]) == 0
    assert (tmp_path / "reg_map.h").is_file() and (tmp_path / "DevRegs.py").is_file()


def test_starter_map_matches_the_workbook():
    pytest.importorskip("openpyxl")
    from regmap.importers.xlsx import import_workbook

    result = import_workbook(EXAMPLE / "Template.xlsm", EXAMPLE / "template.yaml")
    assert result.warnings == []
    assert strip_generator(load_template(TEMPLATE)) == strip_generator(result.yaml_text), (
        "init_template.yaml is out of date: regmap import-xlsx example/Template.xlsm "
        "-o src/regmap/templates/init_template.yaml --force"
    )
