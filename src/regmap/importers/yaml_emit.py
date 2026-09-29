"""Readable YAML writer for imported register maps (the layout of example/vms1511.yaml)."""

import json
from typing import Any

import yaml

HEADER = """\
# Register map {name}
#
# Source of truth for the regmap generator. Derived values are not written here:
#   full name     <BLOCK>_<name>        (SYS + UPTIME -> SYS_UPTIME, C macro CONF_SYS_UPTIME)
#   address       end of the previous register in the block (override: address)
#   register ID   0xBB AAA T A L        (block, address, type, access, log2(size))
#   ENUM min/max  range of its values
#   Modbus        RO/ROF -> input, others -> holding, in order (override: modbus: {{address: N}})
#
# type:   BIN | INT | FLOAT | STRING | ENUM
# access: RO | ROF | RW | RWF | RWIF
# Format: https://github.com/LogicElements/py-reg-map/blob/main/doc/yaml-format.md
"""

REGISTER_KEYS = (
    "name", "type", "access", "size", "address", "label",
    "default", "min", "max", "unit", "modbus", "description",
)  # fmt: skip


class HexInt(int):
    """An integer written back in the hex notation it was read in (e.g. 0x3FF)."""

    text: str

    def __new__(cls, text: str) -> "HexInt":
        obj = super().__new__(cls, int(text, 16))
        obj.text = text
        return obj


def scalar(value: Any, flow: bool = False) -> str:
    """One YAML scalar; strings stay plain when that reads back unchanged, else JSON-quoted."""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, HexInt):
        return value.text
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return yaml.safe_dump(value).split("\n")[0]
    text = str(value)
    probe = f"{{k: {text}}}" if flow else f"k: {text}"
    try:
        plain = text != "" and text == text.strip() and yaml.safe_load(probe) == {"k": text}
    except yaml.YAMLError:
        plain = False
    return text if plain else json.dumps(text, ensure_ascii=False)


def _literal_ok(text: str) -> bool:
    lines = text.split("\n")
    return not text.startswith(" ") and all(line == line.rstrip() for line in lines)


def _field(out: list[str], indent: str, key: str, value: Any) -> None:
    if isinstance(value, str) and "\n" in value and _literal_ok(value):
        out.append(f"{indent}{key}: |-")
        out += [f"{indent}  {line}" if line else "" for line in value.split("\n")]
    else:
        out.append(f"{indent}{key}: {scalar(value)}")


def _item_line(item: dict[str, Any], name_width: int) -> str:
    """One flow mapping per enum value / bit, columns aligned like a table."""
    name = f"name: {item['name']}"
    number = next((k for k in ("value", "bit") if item.get(k) is not None), None)
    texts = [f"{k}: {scalar(item[k], flow=True)}" for k in ("label", "description") if item.get(k)]
    if number is None and not texts:
        return "{" + name + "}"
    line = (name + ",").ljust(name_width + 8)  # "name: " + longest name + "," + one space
    if number is not None:
        segment = f"{number}: {item[number]}"
        line += (segment + ",").ljust(9) if texts else segment
    return "{" + line + ", ".join(texts) + "}"


def _register(out: list[str], reg: dict[str, Any]) -> None:
    first = True
    for key in REGISTER_KEYS:
        if key not in reg:
            continue
        indent = "      - " if first else "        "
        first = False
        value = reg[key]
        if key == "modbus" and isinstance(value, dict):
            inner = ", ".join(f"{k}: {scalar(v, flow=True)}" for k, v in value.items())
            out.append(f"{indent}modbus: {{{inner}}}")
        else:
            _field(out, indent, key, value)
    for key in ("values", "bits"):
        items = reg.get(key)
        if items:
            width = max(len(i["name"]) for i in items)
            out.append(f"        {key}:")
            out += [f"          - {_item_line(i, width)}" for i in items]


def emit_map(doc: dict[str, Any]) -> str:
    """YAML text of a map given as plain data (device, optional generator, blocks)."""
    out = HEADER.format(name=doc["device"]["name"]).rstrip("\n").split("\n")
    out += ["", "device:"]
    out += [f"  {k}: {scalar(v)}" for k, v in doc["device"].items()]
    generator = doc.get("generator") or {}
    if generator:
        out += ["", "generator:"]
        if generator.get("templates"):
            out.append(f"  templates: {scalar(generator['templates'])}")
        if generator.get("outputs"):
            out.append("  outputs:")
            out += [f"    {k}: {scalar(v)}" for k, v in generator["outputs"].items()]
    out += ["", "blocks:"]
    for abbrev, block in doc["blocks"].items():
        out += ["", f"  {abbrev}:", f"    code: {block['code']}"]
        for key in ("name", "description"):
            if block.get(key):
                _field(out, "    ", key, block[key])
        if not block["registers"]:
            out.append("    registers: []")
            continue
        out.append("    registers:")
        for index, reg in enumerate(block["registers"]):
            if index:
                out.append("")
            _register(out, reg)
    return "\n".join(out) + "\n"
