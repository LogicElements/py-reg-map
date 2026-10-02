"""``regmap init``: a new register map from the packaged starter map."""

import re

from regmap.outputs import OUTPUT_KEYS
from regmap.templating import load_template

TEMPLATE = "init_template.yaml"
TEMPLATE_NAME = "Template"  # device name in the starter map (workbook example/Template.xlsm)
_GENERATOR = re.compile(r"^generator:\n(?:[ \t]+.*\n|\n)*", re.MULTILINE)


def generator_block() -> str:
    """Every generator parameter, with all outputs next to the YAML file."""
    lines = ["generator:", "  # templates: templates  # project C templates, see doc/templates.md"]
    lines.append("  outputs:  # directories relative to this file")
    lines += [f"    {key}: ." for key in OUTPUT_KEYS]
    return "\n".join(lines) + "\n\n"


def strip_generator(text: str) -> str:
    """Without the ``generator`` section (its paths depend on where the import was written)."""
    return _GENERATOR.sub("", text)


def new_map_text(name: str) -> str:
    """The starter map for device ``name`` (``\n`` line endings)."""
    text = load_template(TEMPLATE)
    text = strip_generator(text)  # destinations of the starter workbook mean nothing here
    text = text.replace(f"# Register map {TEMPLATE_NAME}\n", f"# Register map {name}\n", 1)
    text = text.replace(f"device:\n  name: {TEMPLATE_NAME}\n", f"device:\n  name: {name}\n", 1)
    return text.replace("\nblocks:\n", f"\n{generator_block()}blocks:\n", 1)
