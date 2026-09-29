import pytest

from regmap.templating import TemplateError, fill, load_template, read_text_file

TEMPLATES = ("reg_map_temp.h", "reg_map_temp.c", "mb_rtu_app_temp.h", "mb_rtu_app_temp.c")


@pytest.mark.parametrize("name", TEMPLATES)
def test_default_templates_are_packaged_and_normalized(name):
    text = load_template(name)
    assert "\r" not in text
    assert text.endswith("\n")
    assert "/* < " in text


def test_project_template_overrides_default(tmp_path):
    (tmp_path / "reg_map_temp.h").write_bytes(b"custom\r\n/* < DEFINE REG MAP > */")
    assert load_template("reg_map_temp.h", tmp_path) == "custom\n/* < DEFINE REG MAP > */\n"
    assert load_template("reg_map_temp.c", tmp_path) == load_template("reg_map_temp.c")


def test_legacy_cp1250_file_is_readable(tmp_path):
    path = tmp_path / "t.h"
    path.write_bytes("// Žluťoučký kůň\n".encode("cp1250"))
    assert read_text_file(path) == "// Žluťoučký kůň\n"


def test_fill_keeps_the_placeholder_line_ending():
    template = "top\n/* < X > */\nbottom\n"
    assert fill(template, {"/* < X > */": "a\nb\n"}, "t") == "top\na\nb\n\nbottom\n"
    assert fill(template, {"/* < X > */": ""}, "t") == "top\n\nbottom\n"


def test_missing_placeholder_is_an_error():
    with pytest.raises(TemplateError, match=r"t\.h: placeholder /\* < Y > \*/ not found"):
        fill("/* < X > */\n", {"/* < X > */": "", "/* < Y > */": ""}, "t.h")
