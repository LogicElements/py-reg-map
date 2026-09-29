"""Fragments for mb_rtu_app.h / mb_rtu_app.c (Modbus register addresses and callbacks)."""

from collections.abc import Iterator

from regmap.codes import RegType
from regmap.generators import fragment, name_width
from regmap.resolve import ResolvedMap, ResolvedRegister

INPUT_DEFINE = "/* < MODBUS INPUT DEFINE > */"
HOLD_DEFINE = "/* < MODBUS HOLD DEFINE > */"
READ_INPUT = "/* < READ INPUT REG > */"
READ_HOLD = "/* < READ HOLD REG > */"
WRITE_HOLD = "/* < WRITE HOLD REG > */"


def _words(rmap: ResolvedMap, space: str) -> Iterator[tuple[ResolvedRegister, int, int, str]]:
    """(register, word index, address, define name) for every Modbus word in ``space``."""
    for r in rmap.registers:
        if r.modbus is None or r.modbus.space != space:
            continue
        many = len(r.modbus.addresses) > 1
        for j, address in enumerate(r.modbus.addresses):
            suffix = f"_{j}" if many else ""
            yield r, j, address, f"MB_{space}_{r.full_name}{suffix}"


def _defines(rmap: ResolvedMap, space: str) -> str:
    width = name_width(rmap)
    first, last = ("MB_INPUT_FIRST    ", "MB_INPUT_LAST     ")
    if space == "HOLD":
        first, last = ("MB_HOLD_FIRST     ", "MB_HOLD_LAST      ")
    last_address = rmap.input_last if space == "INPUT" else rmap.hold_last
    lines = [f"#define {first}0", ""]
    for _, _, address, name in _words(rmap, space):
        pad = " " * max(width + 14 - len(name), 1)
        lines.append(f"#define {name}{pad}{address}u")
    lines += ["", f"#define {last}{last_address}"]
    return fragment(lines)


def _member(r: ResolvedRegister) -> str:
    return f"conf.{r.block.lower()}.{r.c_member}"


def _pointer(rmap: ResolvedMap, r: ResolvedRegister, j: int) -> str:
    return f"*((uint16_t *){rmap.c_prefix}PTR({rmap.c_prefix}{r.full_name}) + {j})"


def _reads(rmap: ResolvedMap, space: str) -> str:
    lines = []
    for r, j, _, name in _words(rmap, space):
        if r.modbus.float_x10:
            expr = f"(int16_t)(10 * {_member(r)})"
        elif len(r.modbus.addresses) > 1:
            expr = _pointer(rmap, r, j)
        else:
            expr = _member(r)
        lines += [f"    case {name}:", f"      *value = {expr};", "      break;"]
    return fragment(lines)


def _writes(rmap: ResolvedMap) -> str:
    lines = []
    for r, j, _, name in _words(rmap, "HOLD"):
        if r.modbus.float_x10:
            assign = f"{_member(r)} = ((float)((int16_t)value)) / 10;"
        elif len(r.modbus.addresses) > 1:
            assign = f"{_pointer(rmap, r, j)} = value;"
        elif r.type is RegType.ENUM:
            assign = f"{_member(r)} = ({r.c_type})value;"
        else:
            assign = f"{_member(r)} = value;"
        lines += [f"    case {name}:", f"      {assign}"]
        if j == len(r.modbus.addresses) - 1:
            lines.append(f"      id = {rmap.c_prefix}{r.full_name};")
        lines.append("      break;")
    return fragment(lines)


def header_fragments(rmap: ResolvedMap) -> dict[str, str]:
    return {INPUT_DEFINE: _defines(rmap, "INPUT"), HOLD_DEFINE: _defines(rmap, "HOLD")}


def source_fragments(rmap: ResolvedMap) -> dict[str, str]:
    return {
        READ_INPUT: _reads(rmap, "INPUT"),
        READ_HOLD: _reads(rmap, "HOLD"),
        WRITE_HOLD: _writes(rmap),
    }
