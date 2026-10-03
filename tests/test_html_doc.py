from html.parser import HTMLParser

from helpers import resolved

from regmap.generators import html_doc

MAP = """
SYS:
  code: 0
  name: System <core>
  description: Basics & more
  registers:
    - {name: VERSION, type: INT, access: ROF, size: 4, label: Version, default: 1001, min: 1001, max: 5050, unit: s, description: "Line one\nLine <two>"}
    - {name: STATUS, type: BIN, access: RO, size: 4, bits: [{name: S_ERR, bit: 0, label: Error}, {name: S_WD, bit: 17}]}
    - {name: MODE, type: ENUM, access: RWF, size: 1, default: M_B, values: [{name: M_A, label: A}, {name: M_B, value: 4}]}
    - {name: TEMP, type: FLOAT, access: RW, size: 4, modbus: {format: F32}}
    - {name: HIDDEN, type: INT, access: RW, size: 2, modbus: false}
EMPTY:
  code: 1
  registers: []
"""


class Rows(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.regs: dict[str, dict[str, str]] = {}

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == "tr" and "id" in a:
            self.regs[a["id"]] = a


def page() -> str:
    return html_doc.render(resolved(MAP))


def test_is_a_complete_document_with_all_registers():
    parser = Rows()
    parser.feed(page())
    assert list(parser.regs) == ["SYS_VERSION", "SYS_STATUS", "SYS_MODE", "SYS_TEMP", "SYS_HIDDEN"]
    assert parser.regs["SYS_MODE"]["data-access"] == "RWF"
    assert parser.regs["SYS_HIDDEN"]["data-modbus"] == "0"
    assert page().startswith("<!DOCTYPE html>") and page().rstrip().endswith("</html>")


def test_shows_derived_values_and_escapes_text():
    text = page()
    assert "0x00000" in text and "System &lt;core&gt;" in text and "Basics &amp; more" in text
    assert "Line &lt;two&gt;" in text and "<two>" not in text
    assert "M_B" in text and "S_WD" in text  # enum values and bits
    assert "EMPTY" not in text  # a block without registers has no section


def test_modbus_map_is_sorted_by_address_and_links_to_registers():
    text = page()
    assert text.index("Input registers") < text.index("Holding registers")
    assert 'href="#SYS_TEMP"' in text and "FLOAT32" in text
    assert 'href="#SYS_HIDDEN"' not in text


def test_output_is_deterministic():
    assert page() == page()
