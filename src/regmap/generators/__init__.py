"""Helpers shared by the output generators."""

from collections.abc import Callable

from regmap.codes import RegType
from regmap.resolve import Item, ResolvedMap, ResolvedRegister


def name_width(rmap: ResolvedMap) -> int:
    """Column width used by the VBA generator: longest register, bit or enum value name."""
    names = [r.full_name for r in rmap.registers]
    names += [item.name for r in rmap.registers for item in r.items]
    return max((len(n) for n in names), default=0)


def fragment(lines: list[str]) -> str:
    """Join lines into a template fragment; every line ends with a newline."""
    return "".join(line + "\n" for line in lines)


def crlf(text: str) -> str:
    """Line breaks inside JSON strings are written as CRLF, as the VBA generator did."""
    return text.replace("\n", "\r\n")


def describe(reg: ResolvedRegister, item_prefix: Callable[[Item], str]) -> str:
    """Register description followed by the legend of its enum values / bits."""
    text = crlf(reg.description)
    if not reg.items:
        return text
    header = (
        "Allowed values: \r\n" if reg.type is RegType.ENUM else "Meaning of respective bits: \r\n"
    )
    text = (text + "\r\n" if text else "") + header
    for item in reg.items:
        label = item.label or item.name
        detail = f" - {crlf(item.description)}" if item.description else ""
        text += f"{item_prefix(item)}{label}{detail}.\r\n"
    return text
