"""<Name>_Modbus.json for the Modbus RTU communication software."""

import json
from typing import Any

from regmap.codes import RegType
from regmap.generators import describe
from regmap.resolve import ResolvedMap, ResolvedRegister


def _item_prefix(reg: ResolvedRegister):
    word = "Value" if reg.type is RegType.ENUM else "Bit"
    return lambda item: f"{word} {item.value} - "


def _entry(reg: ResolvedRegister) -> dict[str, Any]:
    entry: dict[str, Any] = {
        "Type": reg.modbus.space,
        "Name": reg.full_name,
        "Id": reg.id,
        "Address": list(reg.modbus.addresses),
        "Format": reg.modbus.format,
        "Value": 0 if reg.default is None else reg.default,
        "Access": reg.access.value,
        "Min": 0 if reg.range_min is None else reg.range_min,
        "Max": 0 if reg.range_max is None else reg.range_max,
        "Unit": reg.unit,
        "Label": reg.label,
    }
    if reg.type in (RegType.ENUM, RegType.BIN):
        entry["EnumStr"] = [i.label or i.name for i in reg.items]
        entry["EnumValue"] = [i.value for i in reg.items]
    entry["Description"] = describe(reg, _item_prefix(reg))
    return entry


def render(rmap: ResolvedMap) -> str:
    entries = [_entry(r) for r in rmap.registers if r.modbus is not None]
    return json.dumps(entries, indent=2, ensure_ascii=False) + "\n"
