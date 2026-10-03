"""YAML register map: pydantic models, per-object validation and loading."""

from collections.abc import Hashable
from pathlib import Path
from typing import Annotated, Any, Literal

import yaml
from pydantic import (
    AfterValidator,
    BaseModel,
    BeforeValidator,
    ConfigDict,
    Field,
    StrictFloat,
    StrictInt,
    StrictStr,
    StringConstraints,
    ValidationError,
    ValidationInfo,
    field_validator,
    model_validator,
)
from pydantic_core import PydanticCustomError

from regmap.codes import (
    ALLOWED_SIZES,
    MAX_ADDRESS,
    MAX_BLOCK_CODE,
    MAX_MODBUS_ADDRESS,
    Access,
    RegType,
)


class MapError(Exception):
    """One or more problems in a register map; ``errors`` holds one message per problem."""

    def __init__(self, errors: list[str]) -> None:
        super().__init__("\n".join(errors))
        self.errors = errors


def _fail(message: str) -> PydanticCustomError:
    # message goes through the context so braces in it are not treated as a template
    return PydanticCustomError("regmap", "{message}", {"message": message})


def _no_bool(value: Any) -> Any:
    if isinstance(value, bool):
        raise _fail("YAML read this unquoted value as true/false; put the text in quotes")
    return value


def _single_line(value: str) -> str:
    if "\n" in value:
        raise _fail("must be a single line")
    return value


Ident = Annotated[
    str, BeforeValidator(_no_bool), StringConstraints(pattern=r"^[A-Za-z_][A-Za-z0-9_]*$")
]
BlockAbbrev = Annotated[str, StringConstraints(pattern=r"^[A-Z][A-Z0-9_]*$")]
Text = Annotated[str, BeforeValidator(_no_bool)]
Label = Annotated[str, BeforeValidator(_no_bool), AfterValidator(_single_line)]
Number = StrictInt | StrictFloat


class _Strict(BaseModel):
    model_config = ConfigDict(extra="forbid", frozen=True, coerce_numbers_to_str=True)


class EnumValue(_Strict):
    name: Ident
    value: StrictInt | None = Field(default=None, ge=0)
    label: Label | None = None
    description: Text | None = None


class Bit(_Strict):
    name: Ident
    bit: StrictInt = Field(ge=0)
    label: Label | None = None
    description: Text | None = None


class ModbusOptions(_Strict):
    address: StrictInt | None = Field(default=None, ge=0, le=MAX_MODBUS_ADDRESS)
    format: Literal["F32"] | None = None


def enum_numbers(values: list[EnumValue]) -> list[int]:
    """Numeric value of each enum item: explicit ``value`` or previous + 1 (first: 0)."""
    numbers: list[int] = []
    following = 0
    for item in values:
        number = following if item.value is None else item.value
        numbers.append(number)
        following = number + 1
    return numbers


def _duplicates(items: list[Any]) -> list[Any]:
    seen: set[Any] = set()
    return [x for x in items if x in seen or seen.add(x)]


class Register(_Strict):
    # field order matters: validators read earlier fields from info.data
    name: Ident
    type: RegType
    access: Access
    size: StrictInt = Field(ge=1)
    address: StrictInt | None = Field(default=None, ge=0, le=MAX_ADDRESS)
    label: Label | None = None
    description: Text | None = None
    unit: Text | None = None
    min: Number | None = None
    max: Number | None = None
    values: list[EnumValue] | None = None
    bits: list[Bit] | None = None
    modbus: Literal[False] | ModbusOptions | None = None
    default: StrictInt | StrictFloat | StrictStr | None = None

    @field_validator("size")
    @classmethod
    def _check_size(cls, size: int, info: ValidationInfo) -> int:
        reg_type = info.data.get("type")
        allowed = ALLOWED_SIZES.get(reg_type) if reg_type else None
        if allowed is not None and size not in allowed:
            choices = ", ".join(str(s) for s in allowed)
            raise _fail(f"size {size} is not allowed for {reg_type}; use {choices}")
        return size

    @field_validator("min", "max")
    @classmethod
    def _check_range(cls, value: int | float | None, info: ValidationInfo) -> int | float | None:
        if value is None:
            return value
        if info.data.get("type") is RegType.ENUM:
            raise _fail(f"{info.field_name} is derived from values for ENUM; remove it")
        low = info.data.get("min")
        if info.field_name == "max" and low is not None and value < low:
            raise _fail(f"max {value} is lower than min {low}")
        return value

    @field_validator("values")
    @classmethod
    def _check_values(
        cls, values: list[EnumValue] | None, info: ValidationInfo
    ) -> list[EnumValue] | None:
        if values is None:
            return values
        if info.data.get("type") is not RegType.ENUM:
            raise _fail("values are only allowed for ENUM registers")
        if not values:
            raise _fail("ENUM needs at least one value")
        if dup := _duplicates([v.name for v in values]):
            raise _fail(f"duplicate value name {dup[0]}")
        numbers = enum_numbers(values)
        if dup := _duplicates(numbers):
            raise _fail(f"duplicate enum value {dup[0]}")
        size = info.data.get("size")
        if size is not None and max(numbers) >= 1 << (8 * size):
            raise _fail(f"enum value {max(numbers)} does not fit into {size} byte(s)")
        return values

    @field_validator("bits")
    @classmethod
    def _check_bits(cls, bits: list[Bit] | None, info: ValidationInfo) -> list[Bit] | None:
        if bits is None:
            return bits
        if info.data.get("type") is not RegType.BIN:
            raise _fail("bits are only allowed for BIN registers")
        if dup := _duplicates([b.name for b in bits]):
            raise _fail(f"duplicate bit name {dup[0]}")
        if dup := _duplicates([b.bit for b in bits]):
            raise _fail(f"bit {dup[0]} is defined twice")
        size = info.data.get("size")
        if size is not None:
            for b in bits:
                if b.bit >= 8 * size:
                    raise _fail(f"bit {b.bit} ({b.name}) does not fit into {size} byte(s)")
        return bits

    @field_validator("modbus", mode="before")
    @classmethod
    def _modbus_true(cls, value: Any) -> Any:
        if value is True:
            raise _fail("Modbus is on by default; remove the key, or use false / {address: N}")
        return value

    @field_validator("modbus")
    @classmethod
    def _check_modbus(
        cls, modbus: Literal[False] | ModbusOptions | None, info: ValidationInfo
    ) -> Literal[False] | ModbusOptions | None:
        is_f32 = isinstance(modbus, ModbusOptions) and modbus.format == "F32"
        if is_f32 and info.data.get("type") is not RegType.FLOAT:
            raise _fail("format F32 is only allowed for FLOAT registers")
        return modbus

    @field_validator("default")
    @classmethod
    def _check_default(
        cls, default: int | float | str | None, info: ValidationInfo
    ) -> int | float | str | None:
        reg_type = info.data.get("type")
        size = info.data.get("size")
        if default is None or reg_type is None:
            return default
        if reg_type is RegType.ENUM:
            names = [v.name for v in info.data.get("values") or []]
            if not isinstance(default, str) or (names and default not in names):
                raise _fail(f"ENUM default must be one of its value names: {', '.join(names)}")
            return default
        if reg_type is RegType.STRING:
            if not isinstance(default, str):
                raise _fail("STRING default must be text")
            needed = len(default.encode("utf-8")) + 1
            if size is not None and needed > size:
                raise _fail(
                    f"STRING default needs {needed} bytes with the terminating zero, size is {size}"
                )
            return default
        if isinstance(default, str):
            raise _fail(f"{reg_type} default must be a number")
        if reg_type in (RegType.INT, RegType.BIN):
            if not isinstance(default, int):
                raise _fail(f"{reg_type} default must be an integer")
            if size is not None and not 0 <= default < 1 << (8 * size):
                raise _fail(f"default {default} does not fit into {size} unsigned byte(s)")
        low, high = info.data.get("min"), info.data.get("max")
        if low is not None and default < low:
            raise _fail(f"default {default} is below min {low}")
        if high is not None and default > high:
            raise _fail(f"default {default} is above max {high}")
        return default

    @model_validator(mode="after")
    def _enum_needs_values(self) -> "Register":
        if self.type is RegType.ENUM and not self.values:
            raise _fail("ENUM register needs values")
        return self


class Block(_Strict):
    code: StrictInt = Field(ge=0, le=MAX_BLOCK_CODE)
    name: Text | None = None
    description: Text | None = None
    registers: list[Register]


class Device(_Strict):
    name: Ident
    c_prefix: Ident = "CONF_"
    c_storage: Ident = "CONF_REG"


class Outputs(_Strict):
    reg_map: Text | None = None
    modbus: Text | None = None
    lebin_json: Text | None = None
    modbus_json: Text | None = None
    python: Text | None = None
    html: Text | Literal[False] | None = None  # False: do not write the HTML table


class GeneratorSettings(_Strict):
    templates: Text | None = None
    outputs: Outputs = Field(default_factory=Outputs)


class RegisterMap(_Strict):
    device: Device
    generator: GeneratorSettings = Field(default_factory=GeneratorSettings)
    blocks: dict[BlockAbbrev, Block]


# ---------------------------------------------------------------- loading


class _UniqueKeyLoader(yaml.SafeLoader):
    """SafeLoader that rejects duplicate mapping keys instead of silently keeping the last."""

    def construct_mapping(self, node: yaml.MappingNode, deep: bool = False) -> dict[Any, Any]:
        seen: set[Any] = set()
        for key_node, _ in node.value:
            key = self.construct_object(key_node, deep=deep)
            if isinstance(key, Hashable):
                if key in seen:
                    raise yaml.constructor.ConstructorError(
                        "while constructing a mapping",
                        node.start_mark,
                        f"found duplicate key {key!r}",
                        key_node.start_mark,
                    )
                seen.add(key)
        return super().construct_mapping(node, deep=deep)


# pydantic adds union member names to error locations; they mean nothing to a map author
_UNION_TAGS = {"int", "float", "str", "bool", "ModbusOptions"}


def _clean_loc(loc: tuple[int | str, ...]) -> tuple[int | str, ...]:
    return tuple(x for x in loc if not (isinstance(x, str) and (x in _UNION_TAGS or "[" in x)))


def _loc_text(loc: tuple[int | str, ...], data: Any) -> str:
    text = ""
    node = data
    for item in loc:
        text += f"[{item}]" if isinstance(item, int) else (f".{item}" if text else str(item))
        try:
            node = node[item]
        except (KeyError, IndexError, TypeError):
            node = None
        if isinstance(item, int) and isinstance(node, dict) and isinstance(node.get("name"), str):
            text += f" ({node['name']})"
    return text


def _format_errors(exc: ValidationError, data: Any) -> list[str]:
    by_loc: dict[tuple[int | str, ...], list[str]] = {}
    for err in exc.errors():
        msg = "unknown key" if err["type"] == "extra_forbidden" else err["msg"]
        messages = by_loc.setdefault(_clean_loc(err["loc"]), [])
        if msg not in messages:
            messages.append(msg)
    locs = list(by_loc)
    # an error on a parent is noise when a union member reported something more specific inside
    kept = [loc for loc in locs if not any(o != loc and o[: len(loc)] == loc for o in locs)]
    return [f"{_loc_text(loc, data)}: {' / '.join(by_loc[loc])}" for loc in kept]


def load_map_text(text: str, source: str = "<string>") -> RegisterMap:
    try:
        data = yaml.load(text, Loader=_UniqueKeyLoader)  # noqa: S506 - SafeLoader subclass
    except yaml.YAMLError as exc:
        raise MapError([f"{source}: {exc}"]) from None
    if not isinstance(data, dict):
        raise MapError([f"{source}: top level must be a mapping with 'device' and 'blocks'"])
    try:
        return RegisterMap.model_validate(data)
    except ValidationError as exc:
        raise MapError(_format_errors(exc, data)) from None


def load_map(path: Path) -> RegisterMap:
    return load_map_text(path.read_text(encoding="utf-8-sig"), str(path))
