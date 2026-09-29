"""Compute every derived value of a register map once (addresses, IDs, C types, Modbus)."""

from collections.abc import Iterator
from dataclasses import dataclass

from regmap.codes import (
    FLASH_ACCESS,
    INPUT_ACCESS,
    MAX_ADDRESS,
    MAX_MODBUS_ADDRESS,
    Access,
    RegType,
    register_id,
)
from regmap.model import Block, MapError, ModbusOptions, Register, RegisterMap, enum_numbers

C_UINT = {1: "uint8_t", 2: "uint16_t", 4: "uint32_t", 8: "uint64_t"}


@dataclass(frozen=True)
class Item:
    """An enum value or a bit of a register."""

    name: str
    value: int  # enum value or bit number
    label: str  # "" when not given
    description: str  # "" when not given


@dataclass(frozen=True)
class ModbusPlacement:
    space: str  # "INPUT" or "HOLD"
    addresses: tuple[int, ...]
    format: str  # Modbus JSON "Format": the register type, or "FLOAT32"
    float_x10: bool  # FLOAT transported as int16 * 10


@dataclass(frozen=True)
class ResolvedRegister:
    block: str  # block abbreviation, e.g. "SYS"
    block_code: int
    name: str  # e.g. "UPTIME"
    full_name: str  # e.g. "SYS_UPTIME"
    type: RegType
    access: Access
    size: int
    address: int
    id: int
    label: str
    description: str
    unit: str
    default: int | float | str | None  # ENUM default as its number
    min: int | float | None  # as written in YAML (never set for ENUM)
    max: int | float | None
    range_min: int | float | None  # ENUM: smallest value, otherwise == min
    range_max: int | float | None  # ENUM: largest value, otherwise == max
    values: tuple[Item, ...]
    bits: tuple[Item, ...]
    modbus: ModbusPlacement | None  # None: not exposed on Modbus
    c_type: str  # "uint32_t", "float", "com_mb_baud_rate_t", ...
    c_member: str  # lower-case register name
    c_array: int | None  # array length for sizes other than 1/2/4/8

    @property
    def items(self) -> tuple[Item, ...]:
        return self.values if self.type is RegType.ENUM else self.bits


@dataclass(frozen=True)
class ResolvedBlock:
    abbrev: str
    code: int
    registers: tuple[ResolvedRegister, ...]
    end: int  # max(address + size); 0 for a block without registers


@dataclass(frozen=True)
class ResolvedMap:
    name: str
    c_prefix: str
    c_storage: str
    blocks: tuple[ResolvedBlock, ...]  # ascending code
    flash: tuple[ResolvedRegister, ...]
    calib_serial: ResolvedRegister | None  # FACT_SERIAL_NUMBER, always first in the calib list
    calib: tuple[ResolvedRegister, ...]  # RWIF registers (without the serial number)
    input_last: int  # highest INPUT address, -1 if none
    hold_last: int  # highest HOLD address, -1 if none

    @property
    def registers(self) -> Iterator[ResolvedRegister]:
        for block in self.blocks:
            yield from block.registers

    def block_by_code(self, code: int) -> ResolvedBlock | None:
        return next((b for b in self.blocks if b.code == code), None)


def _c_type(full_name: str, reg: Register) -> tuple[str, int | None]:
    if reg.type is RegType.ENUM:
        return f"{full_name.lower()}_t", None
    if reg.type is RegType.FLOAT:
        return "float", None
    if reg.size in C_UINT:
        return C_UINT[reg.size], None
    return "uint8_t", reg.size


def _word_count(reg: Register, f32: bool) -> int:
    if reg.type is RegType.ENUM:
        return 1
    if reg.type is RegType.FLOAT:
        return 2 if f32 else 1
    return (reg.size + 1) // 2


def _text(value: str | None) -> str:
    return value or ""


class _Resolver:
    def __init__(self, rmap: RegisterMap) -> None:
        self.rmap = rmap
        self.errors: list[str] = []
        self.full_names: dict[str, str] = {}  # lower-case full name -> path
        self.item_names: dict[str, str] = {}  # enum value / bit name -> path
        self.counter = {"INPUT": 0, "HOLD": 0}
        self.used: dict[str, dict[int, str]] = {"INPUT": {}, "HOLD": {}}

    def run(self) -> ResolvedMap:
        codes: dict[int, str] = {}
        for abbrev, block in self.rmap.blocks.items():
            if block.code in codes:
                owner = codes[block.code]
                self.errors.append(f"blocks.{abbrev}.code: {block.code} is already used by {owner}")
            codes.setdefault(block.code, abbrev)
        ordered = sorted(self.rmap.blocks.items(), key=lambda kv: kv[1].code)
        blocks = tuple(self._block(abbrev, block) for abbrev, block in ordered)
        if self.errors:
            raise MapError(self.errors)
        regs = [r for b in blocks for r in b.registers]
        serial = next((r for r in regs if r.full_name == "FACT_SERIAL_NUMBER"), None)
        device = self.rmap.device
        return ResolvedMap(
            name=device.name,
            c_prefix=device.c_prefix,
            c_storage=device.c_storage,
            blocks=blocks,
            flash=tuple(r for r in regs if r.access in FLASH_ACCESS),
            calib_serial=serial,
            calib=tuple(r for r in regs if r.access is Access.RWIF and r is not serial),
            input_last=max(self.used["INPUT"], default=-1),
            hold_last=max(self.used["HOLD"], default=-1),
        )

    def _block(self, abbrev: str, block: Block) -> ResolvedBlock:
        offset = 0
        registers = []
        for index, reg in enumerate(block.registers):
            path = f"blocks.{abbrev}.registers[{index}] ({reg.name})"
            address = offset if reg.address is None else reg.address
            if address < offset:
                self.errors.append(
                    f"{path}.address: {address} overlaps the previous register ending at {offset}"
                )
            if address + reg.size > MAX_ADDRESS + 1:
                self.errors.append(
                    f"{path}: ends at {address + reg.size}, beyond the 4096-byte block"
                )
            offset = max(offset, address + reg.size)
            registers.append(self._register(abbrev, block.code, reg, address, path))
        return ResolvedBlock(abbrev, block.code, tuple(registers), offset)

    def _unique(self, table: dict[str, str], key: str, shown: str, path: str) -> None:
        if key in table:
            self.errors.append(f"{path}: name {shown} is already used at {table[key]}")
        else:
            table[key] = path

    def _register(
        self, abbrev: str, code: int, reg: Register, address: int, path: str
    ) -> ResolvedRegister:
        full = f"{abbrev}_{reg.name}"
        self._unique(self.full_names, full.lower(), full, f"{path}.name")
        values = ()
        if reg.type is RegType.ENUM and reg.values:
            numbers = enum_numbers(reg.values)
            values = tuple(
                Item(v.name, n, _text(v.label), _text(v.description))
                for v, n in zip(reg.values, numbers, strict=True)
            )
        bits = tuple(
            Item(b.name, b.bit, _text(b.label), _text(b.description)) for b in reg.bits or ()
        )
        for kind, items in (("values", values), ("bits", bits)):
            for i, item in enumerate(items):
                self._unique(self.item_names, item.name, item.name, f"{path}.{kind}[{i}]")
        default = reg.default
        range_min, range_max = reg.min, reg.max
        if reg.type is RegType.ENUM and values:
            if isinstance(default, str):
                default = next(v.value for v in values if v.name == default)
            range_min = min(v.value for v in values)
            range_max = max(v.value for v in values)
        c_type, c_array = _c_type(full, reg)
        return ResolvedRegister(
            block=abbrev,
            block_code=code,
            name=reg.name,
            full_name=full,
            type=reg.type,
            access=reg.access,
            size=reg.size,
            address=address,
            id=register_id(code, address, reg.type, reg.access, reg.size),
            label=_text(reg.label),
            description=_text(reg.description),
            unit=_text(reg.unit),
            default=default,
            min=reg.min,
            max=reg.max,
            range_min=range_min,
            range_max=range_max,
            values=values,
            bits=bits,
            modbus=self._modbus(reg, full, path),
            c_type=c_type,
            c_member=reg.name.lower(),
            c_array=c_array,
        )

    def _modbus(self, reg: Register, full: str, path: str) -> ModbusPlacement | None:
        if reg.modbus is False:
            return None
        options = reg.modbus if isinstance(reg.modbus, ModbusOptions) else ModbusOptions()
        f32 = options.format == "F32"
        space = "INPUT" if reg.access in INPUT_ACCESS else "HOLD"
        if options.address is not None:
            self.counter[space] = options.address
        start = self.counter[space]
        addresses = tuple(range(start, start + _word_count(reg, f32)))
        self.counter[space] = start + len(addresses)
        for a in addresses:
            if a > MAX_MODBUS_ADDRESS:
                self.errors.append(f"{path}.modbus: address {a} is beyond {MAX_MODBUS_ADDRESS}")
            elif a in self.used[space]:
                self.errors.append(
                    f"{path}.modbus: {space} address {a} is already used by {self.used[space][a]}"
                )
            else:
                self.used[space][a] = full
        fmt = "FLOAT32" if f32 else reg.type.value
        return ModbusPlacement(space, addresses, fmt, reg.type is RegType.FLOAT and not f32)


def resolve(rmap: RegisterMap) -> ResolvedMap:
    """Resolve a validated map; raises MapError listing every cross-object problem."""
    return _Resolver(rmap).run()
