"""<Name>_registers.json for the LeBin protocol communication software."""

import json
from typing import Any

from regmap.codes import Access, RegType
from regmap.generators import describe
from regmap.resolve import Item, ResolvedMap, ResolvedRegister


def _item_prefix(reg: ResolvedRegister):
    if reg.type is RegType.BIN:
        return lambda item: f"[{item.value}] - "
    return lambda item: ""


def _entry(reg: ResolvedRegister) -> dict[str, Any]:
    value = "" if reg.default is None else str(reg.default)
    entry: dict[str, Any] = {
        "Category": reg.block,
        "Name": reg.name,
        "Label": reg.label,
        "Id": reg.id,
        "VarType": reg.type.value,
        "Access": Access.RWF.value if reg.access is Access.RWIF else reg.access.value,
        "Description": describe(reg, _item_prefix(reg)),
        "Value": value,
        "NewValue": value,
    }
    if reg.type in (RegType.ENUM, RegType.BIN):
        entry["EnumStr"] = [_label(i) for i in reg.items]
        entry["EnumValue"] = [i.value for i in reg.items]
    entry["Reset"] = value
    if reg.min is not None:
        entry["Min"] = reg.min
    if reg.max is not None:
        entry["Max"] = reg.max
    entry["Units"] = reg.unit
    return entry


def _label(item: Item) -> str:
    return item.label or item.name


def render(rmap: ResolvedMap) -> str:
    return json.dumps([_entry(r) for r in rmap.registers], indent=2, ensure_ascii=False) + "\n"
