"""<Name>Regs.py: register names and enum values for Python tests."""

from regmap.codes import RegType
from regmap.resolve import ResolvedMap


def render(rmap: ResolvedMap) -> str:
    lines = [f"class {rmap.name}Regs:"]
    lines += [f'    {r.full_name} = "{r.full_name}"' for r in rmap.registers] or ["    pass"]
    text = "\n".join(lines) + "\n\n\n"
    for r in rmap.registers:
        if r.type is RegType.ENUM:
            text += f"class {r.full_name}:\n"
            text += "".join(f"    {v.name} = {v.value}\n" for v in r.values)
            text += "\n\n"
    return text
