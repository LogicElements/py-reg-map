"""Fragments for reg_map.h / reg_map.c (register IDs, storage structures, factory values)."""

from regmap.codes import BLOCK_COUNT, RegType
from regmap.generators import fragment, name_width
from regmap.resolve import ResolvedBlock, ResolvedMap, ResolvedRegister

REG_MAP = "/* < DEFINE REG MAP > */"
REG_BITS = "/* < DEFINE REG BITS > */"
REG_PARAMS = "/* < REG MAP PARAMS > */"
REG_TYPEDEFS = "/* < REG MAP TYPEDEFS > */"
REG_STORAGE = "/* < DEFINE REG MAP STORAGE > */"
REG_FACTORY = "/* < REG MAP FACTORY > */"


def struct_name(rmap: ResolvedMap, block: ResolvedBlock) -> str:
    return f"{rmap.c_storage}_{block.abbrev}".lower() + "_t"


def _define_map(rmap: ResolvedMap) -> str:
    width = name_width(rmap)
    lines = []
    for r in rmap.registers:
        pad = " " * (1 + width - len(r.full_name))
        lines.append(f"#define {rmap.c_prefix}{r.full_name}{pad}0x{r.id:08X}u  ///< {r.label}")
    return fragment(lines)


def _define_bits(rmap: ResolvedMap) -> str:
    lines = []
    for r in rmap.registers:
        for bit in r.bits:
            pad = " " * max(30 - len(bit.name), 3)
            lines.append(f"#define {bit.name.upper()}{pad}(1 << ({bit.value}))")
    return fragment(lines)


def _params(rmap: ResolvedMap) -> str:
    s = rmap.c_storage
    calib_number = len(rmap.calib) + (1 if rmap.calib_serial else 0)
    calib_length = sum(r.size + 4 for r in rmap.calib) + (8 if rmap.calib_serial else 0)
    flash_length = sum(r.size + 4 for r in rmap.flash)
    used = [b for b in rmap.blocks if b.registers]
    checks = "".join(
        f"(sizeof({struct_name(rmap, b)}) != {(b.end + 3) // 4 * 4}) || " for b in used
    )
    return fragment(
        [
            f"#define {s}_BLOCK_NUMBER      ({BLOCK_COUNT})",
            f"#define {s}_LOGGER_NUMBER     (0)",
            f"#define {s}_CALIB_NUMBER      ({calib_number})",
            f"#define {s}_SYNCED_NUMBER     (0)",
            f"#define {s}_FLASH_NUMBER      ({len(rmap.flash)})",
            f"#define {s}_LOGGER_LENGTH     (0)",
            f"#define {s}_CALIB_LENGTH      ({calib_length})",
            f"#define {s}_SYNCED_LENGTH     (0)",
            f"#define {s}_FLASH_LENGTH      ({flash_length})",
            f"#define {s}_LOCAL_LENGTH      (0)",
            "",
            f"#define CONF_DIM_CONDITION ({checks}0)",
        ]
    )


def _members(block: ResolvedBlock) -> list[str]:
    lines = []
    offset = 0
    reserved = 0
    for r in block.registers:
        if r.address > offset:
            lines.append(f"  uint8_t reserved{reserved}[{r.address - offset}];")
            reserved += 1
        array = f"[{r.c_array}]" if r.c_array else ""
        lines.append(f"  {r.c_type} {r.c_member}{array};")
        offset = r.address + r.size
    return lines


def _typedefs(rmap: ResolvedMap) -> str:
    lines = [""]
    for r in rmap.registers:
        if r.type is RegType.ENUM:
            lines += ["typedef enum", "{"]
            lines += [f"  {v.name} = {v.value}," for v in r.values]
            lines += [f"}}{r.c_type} ;", ""]
    for b in rmap.blocks:
        if b.registers:
            lines += ["typedef struct __packed __aligned(4)", "{"]
            lines += _members(b)
            lines += [f"}}{struct_name(rmap, b)};", ""]
    lines += ["", "typedef struct ", "{"]
    for code in range(BLOCK_COUNT):
        b = rmap.block_by_code(code)
        if b is not None and b.registers:
            lines.append(f"  {struct_name(rmap, b)} {b.abbrev.lower()};")
        else:
            lines.append(f"  uint32_t res{code + 1};")
    lines += ["}", f"{rmap.c_storage.lower()}_t;", ""]
    return fragment(lines)


def _array(rmap: ResolvedMap, suffix: str, items: list[str]) -> list[str]:
    s = rmap.c_storage
    return [f"const uint32_t {s}_{suffix}[{s}_{suffix}_NUMBER] = {{", "".join(items) + "};", ""]


def _storage(rmap: ResolvedMap) -> str:
    s = rmap.c_storage
    p = rmap.c_prefix
    pointers = []
    limits = []
    for code in range(BLOCK_COUNT):
        b = rmap.block_by_code(code)
        used = b is not None and bool(b.registers)
        pointers.append(f"(uint8_t*)&conf.{b.abbrev.lower()}, " if used else "NULL, ")
        limits.append(f"{b.end if used else 0}, ")
    calib = ([rmap.calib_serial] if rmap.calib_serial else []) + list(rmap.calib)
    lines = [f"{s.lower()}_t conf;", "", ""]
    lines += [f"uint8_t* const {s}[{s}_BLOCK_NUMBER] = {{{''.join(pointers)}}};", ""]
    lines += [f"const uint32_t {s}_LIMIT[{s}_BLOCK_NUMBER] = {{", "".join(limits) + "};", ""]
    lines += _array(rmap, "FLASH", [f"{p}{r.full_name}, " for r in rmap.flash])
    lines += _array(rmap, "LOGGER", [])
    lines += _array(rmap, "CALIB", [f"{p}{r.full_name}, " for r in calib])
    lines += _array(rmap, "SYNCED", [])
    return fragment(lines)


def c_float(value: float) -> str:
    """Format like VBA Format(v, "###0.0#####"): 1 to 6 decimals."""
    text = f"{float(value):.6f}".rstrip("0")
    return text + "0" if text.endswith(".") else text


def c_string(text: str) -> str:
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')
    escaped = escaped.replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")
    return f'"{escaped}"'


def _factory_line(rmap: ResolvedMap, r: ResolvedRegister, width: int) -> str:
    p = rmap.c_prefix
    if r.type is RegType.STRING:
        literal = c_string(str(r.default))
        return f"  memcpy({p}PTR({p}{r.full_name}), {literal}, sizeof({literal}));"
    if r.type is RegType.FLOAT:
        macro, value = "FLOAT", c_float(r.default)
    else:
        macro = {1: "BYTE", 2: "SHORT"}.get(r.size, "INT")
        value = str(r.default)
    left = f"{p}{macro}({p}{r.full_name})"
    column = len(f"{p}SHORT(") + len(p) + width + 1
    return f"  {left.ljust(column)} = {value};"


def _factory(rmap: ResolvedMap) -> str:
    width = name_width(rmap)
    return fragment(
        [_factory_line(rmap, r, width) for r in rmap.registers if r.default is not None]
    )


def header_fragments(rmap: ResolvedMap) -> dict[str, str]:
    return {
        REG_MAP: _define_map(rmap),
        REG_BITS: _define_bits(rmap),
        REG_PARAMS: _params(rmap),
        REG_TYPEDEFS: _typedefs(rmap),
    }


def source_fragments(rmap: ResolvedMap) -> dict[str, str]:
    return {REG_STORAGE: _storage(rmap), REG_FACTORY: _factory(rmap)}
