# py-reg-map Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the `regmap` Python CLI that generates C storage/Modbus sources and LeBin/Modbus JSON from a YAML register map, plus an importer for the legacy Excel/VBA workbooks.

**Architecture:** YAML → pydantic model (per-object validation) → `resolve()` computes every derived value once (addresses, IDs, C types, Modbus allocation) → independent generators produce C fragments (filled into packaged, overridable templates) and JSON/Python text → a writer emits UTF-8/CRLF files after a stability check against the previous JSON outputs. All code below was run in a scratch prototype: the generated `reg_map.h/.c`, `mb_rtu_app.h/.c` and `Vms1511Regs.py` are byte-identical to the VBA reference files in `example/`, and every task stage below was verified to pass the full suite, `ruff check` and `ruff format --check`.

**Tech Stack:** Python ≥ 3.12, pydantic 2, PyYAML, openpyxl (optional `xlsx` extra), pytest, ruff, hatchling, GitHub Actions.

**Spec:** `doc/design/specs/2026-09-28-py-reg-map-design.md`

## Global Constraints

- Python `>=3.12`; runtime dependencies exactly `pyyaml>=6`, `pydantic>=2.6`; optional extra `xlsx = ["openpyxl>=3.1"]`; dev extra `pytest>=8`, `ruff>=0.6`, `openpyxl>=3.1`.
- Package `py-reg-map`, import package `regmap`, console script `regmap = "regmap.cli:main"`, build backend `hatchling`.
- All generated files: UTF-8 without BOM, CRLF line endings (`regmap.writer.encode`).
- Code, comments, messages and documentation in English. `README.md` holds only what the package is, installation and usage; every behavior/format detail goes to `doc/*.md`.
- Specs and plans live in `doc/design/specs/` and `doc/design/plans/` (not `docs/`).
- ruff: `line-length = 100`, `target-version = "py312"`, rules `E, F, W, I, UP, B`, `example/` excluded, `E501` ignored in `tests/`.
- Exit codes: `0` success, `1` invalid map / template error / breaking change / `--check` difference / import failure, `2` bad arguments or missing input file.
- `example/` (company data) is committed only in Task 13; nothing else outside each task's file list is committed.
- Run every command from the repository root with the virtual environment active. Push with `git -c credential.helper= -c 'credential.helper=!gh auth git-credential' -c credential.interactive=never push` (plain `git push` hangs on the Git Credential Manager prompt on this machine).

## Review Focus

1. Running `regmap generate ../project/dev.yaml` from another directory must put every output next to the YAML file (or under its `generator.outputs`), never into the current directory — `tests/test_cli.py::test_relative_map_path_writes_next_to_the_map` (Task 10).
2. In an Excel workbook, enum items placed after a `stop` separator row must not be attached to the register above the separator; they are ignored with a warning — `tests/test_import_xlsx.py::test_items_after_a_stop_row_are_not_attached` (Task 12).
3. A block declared with `registers: []` must behave like an unused code: `NULL` pointer, `res<N>` slot, `LIMIT` 0 and no `sizeof` check — `tests/test_c_regmap.py::test_block_without_registers_is_an_unused_slot` (Task 5).
4. `generator.outputs` pointing to a directory that does not exist yet must be created instead of failing — `tests/test_outputs.py::test_writer_creates_missing_destination_directories` (Task 8).
5. Very long registers (a 2048-byte STRING has Modbus words `_0` … `_1023`) must keep at least one space between the `#define` name and the address — `tests/test_c_modbus.py::test_huge_register_still_has_a_space_before_the_address` (Task 6).

---

### Task 1: Project scaffold and register ID codes

**Files:**
- Create: `pyproject.toml`
- Create: `.gitignore`
- Create: `.gitattributes`
- Create: `.github/workflows/ci.yml`
- Create: `README.md`
- Create: `src/regmap/__init__.py`
- Create: `src/regmap/codes.py`
- Test: `tests/test_codes.py`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `regmap.__version__: str` (from package metadata, `"0.0.0"` when not installed).
  - `regmap.codes`: `BLOCK_COUNT = 26`, `MAX_BLOCK_CODE = 25`, `MAX_ADDRESS = 0xFFF`,
    `MAX_MODBUS_ADDRESS = 0xFFFF`; `class RegType(StrEnum)`: `BIN INT FLOAT STRING ENUM`;
    `class Access(StrEnum)`: `RO ROF RW RWF RWIF`; `TYPE_CODE`, `ACCESS_CODE`,
    `ALLOWED_SIZES: dict[RegType, tuple[int, ...] | None]`, `FLASH_ACCESS`, `INPUT_ACCESS`
    (frozensets of `Access`); `register_id(block_code: int, address: int, reg_type: RegType,
    access: Access, size: int) -> int`.

- [ ] **Step 1: Create the configuration files**

Create `pyproject.toml`:

```toml
[build-system]
requires = ["hatchling"]
build-backend = "hatchling.build"

[project]
name = "py-reg-map"
version = "0.1.0"
description = "Register map generator for embedded firmware (C storage, LeBin and Modbus RTU JSON)"
readme = "README.md"
requires-python = ">=3.12"
dependencies = ["pyyaml>=6", "pydantic>=2.6"]

[project.optional-dependencies]
xlsx = ["openpyxl>=3.1"]
dev = ["pytest>=8", "ruff>=0.6", "openpyxl>=3.1"]

[project.scripts]
regmap = "regmap.cli:main"

[tool.hatch.build.targets.wheel]
packages = ["src/regmap"]

[tool.ruff]
line-length = 100
target-version = "py312"
extend-exclude = ["example"]  # VBA reference outputs are kept byte-for-byte

[tool.ruff.lint]
select = ["E", "F", "W", "I", "UP", "B"]

[tool.ruff.lint.per-file-ignores]
"tests/*" = ["E501"]  # inline YAML test data reads better unwrapped

[tool.pytest.ini_options]
testpaths = ["tests"]
pythonpath = ["tests"]
```

Create `.gitignore`:

```text
__pycache__/
*.py[cod]
*.egg-info/
.venv/
venv/
build/
dist/
.pytest_cache/
.ruff_cache/
```

Create `.gitattributes`:

```text
* text=auto

# Reference files and templates are compared byte-for-byte (CRLF): never convert them.
example/** -text
src/regmap/templates/** -text
```

Create `.github/workflows/ci.yml`:

```yaml
name: CI

on:
  push:
  pull_request:

jobs:
  test:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        python-version: ["3.12", "3.13", "3.14"]
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: ${{ matrix.python-version }}
      - run: pip install -e ".[dev]"
      - run: ruff check .
      - run: ruff format --check .
      - run: pytest
```

Create `README.md`:

````markdown
# py-reg-map

Register map generator for embedded firmware. A YAML file describes the device registers;
`regmap` generates the C storage and Modbus RTU sources for the firmware (`reg_map.h/.c`,
`mb_rtu_app.h/.c`) and the JSON definitions for the LeBin and Modbus RTU communication
software.

## Installation

Python 3.12 or newer:

```sh
pip install "py-reg-map @ git+https://github.com/LogicElements/py-reg-map"
pip install "py-reg-map[xlsx] @ git+https://github.com/LogicElements/py-reg-map"   # with Excel import
```
````

Create `src/regmap/__init__.py`:

```python
"""Register map generator for embedded firmware."""

from importlib.metadata import PackageNotFoundError, version

try:
    __version__ = version("py-reg-map")
except PackageNotFoundError:  # running from a source tree without installation
    __version__ = "0.0.0"
```

- [ ] **Step 2: Create the virtual environment and install the package**

The repository already contains `doc/design/` and the untracked folders `example/` and
`template/` (they stay untracked until Tasks 13 and 4).

```bash
python -m venv .venv
source .venv/Scripts/activate   # Git Bash on Windows; Linux/macOS: source .venv/bin/activate
python -m pip install -e ".[dev]"
```

Expected: pip ends with `Successfully installed ... py-reg-map-0.1.0 ...`.

- [ ] **Step 3: Write the failing tests**

Create `tests/test_codes.py`:

```python
import pytest

from regmap.codes import Access, RegType, register_id


@pytest.mark.parametrize(
    ("code", "address", "reg_type", "access", "size", "expected"),
    [
        (0, 0, RegType.INT, Access.RO, 4, 0x00000112),  # SYS_UPTIME
        (0, 4, RegType.INT, Access.ROF, 4, 0x00004132),  # SYS_REGMAP_VERSION
        (0, 8, RegType.BIN, Access.RO, 4, 0x00008012),  # SYS_STATUS
        (0, 20, RegType.INT, Access.RO, 2, 0x00014111),  # SYS_IO_INPUT
        (3, 0, RegType.ENUM, Access.RWF, 1, 0x03000570),  # COM_MB_BAUD_RATE
        (3, 3, RegType.INT, Access.RW, 1, 0x03003150),  # COM_RESERVED
        (3, 8, RegType.INT, Access.RWF, 2, 0x03008171),  # COM_MB_TIMEOUT
        (6, 0, RegType.INT, Access.RO, 4, 0x06000112),  # DBG_WRITES_CONF
    ],
)
def test_register_id_matches_vms1511(code, address, reg_type, access, size, expected):
    assert register_id(code, address, reg_type, access, size) == expected


def test_rwif_has_the_same_access_code_as_rwf():
    assert register_id(4, 0, RegType.INT, Access.RWIF, 4) == register_id(
        4, 0, RegType.INT, Access.RWF, 4
    )


@pytest.mark.parametrize(
    ("size", "log2"), [(1, 0), (2, 1), (3, 1), (4, 2), (8, 3), (16, 4), (20, 4)]
)
def test_size_field_is_floor_log2(size, log2):
    assert register_id(0, 0, RegType.STRING, Access.RO, size) & 0xF == log2
```

- [ ] **Step 4: Run the tests to verify they fail**

Run: `pytest -q tests/test_codes.py`
Expected: FAIL — `ModuleNotFoundError: No module named 'regmap.codes'`

- [ ] **Step 5: Implement**

Create `src/regmap/codes.py` with:

```python
"""Fixed register-map vocabulary: data types, access types and register ID composition."""

from enum import StrEnum

BLOCK_COUNT = 26
MAX_BLOCK_CODE = BLOCK_COUNT - 1
MAX_ADDRESS = 0xFFF
MAX_MODBUS_ADDRESS = 0xFFFF


class RegType(StrEnum):
    BIN = "BIN"
    INT = "INT"
    FLOAT = "FLOAT"
    STRING = "STRING"
    ENUM = "ENUM"


class Access(StrEnum):
    RO = "RO"
    ROF = "ROF"
    RW = "RW"
    RWF = "RWF"
    RWIF = "RWIF"


TYPE_CODE = {
    RegType.BIN: 0,
    RegType.INT: 1,
    RegType.FLOAT: 2,
    RegType.STRING: 3,
    RegType.ENUM: 5,
}

ACCESS_CODE = {
    Access.RO: 0,
    Access.ROF: 1,
    Access.RW: 2,
    Access.RWF: 3,
    Access.RWIF: 3,
}

# None means "any size >= 1"
ALLOWED_SIZES: dict[RegType, tuple[int, ...] | None] = {
    RegType.BIN: (1, 2, 4, 8),
    RegType.INT: (1, 2, 4, 8),
    RegType.FLOAT: (4,),
    RegType.ENUM: (1, 2, 4),
    RegType.STRING: None,
}

FLASH_ACCESS = frozenset({Access.RWF, Access.ROF})
INPUT_ACCESS = frozenset({Access.RO, Access.ROF})


def register_id(block_code: int, address: int, reg_type: RegType, access: Access, size: int) -> int:
    """Compose the 32-bit register ID 0xBB_AAA_T_A_L."""
    log2_size = size.bit_length() - 1  # floor(log2(size)) for size >= 1
    return (
        (block_code << 24)
        | (address << 12)
        | (TYPE_CODE[reg_type] << 8)
        | ((1 + 2 * ACCESS_CODE[access]) << 4)
        | log2_size
    )
```

- [ ] **Step 6: Run the whole suite and the linters**

Run: `pytest -q && ruff check . && ruff format --check .`
Expected: `16 passed` (16 of them in this task's tests), `All checks passed!`, all files already formatted.

- [ ] **Step 7: Commit**

```bash
git add pyproject.toml .gitignore .gitattributes .github/workflows/ci.yml README.md src/regmap/__init__.py src/regmap/codes.py tests/test_codes.py
git commit -m "chore: project scaffold with register ID codes"
```

### Task 2: YAML model, validation and loading

**Files:**
- Create: `src/regmap/model.py`
- Create: `tests/helpers.py`
- Create: `doc/yaml-format.md`
- Test: `tests/test_model.py`

**Interfaces:**
- Consumes: `regmap.codes` (Task 1).
- Produces (`regmap.model`):
  - `class MapError(Exception)` with `errors: list[str]` (one message per problem).
  - pydantic models (frozen, unknown keys forbidden): `EnumValue(name, value, label,
    description)`, `Bit(name, bit, label, description)`, `ModbusOptions(address, format)`,
    `Register(name, type, access, size, address, label, description, unit, min, max, values,
    bits, modbus, default)` where `modbus: Literal[False] | ModbusOptions | None`,
    `Block(code, name, description, registers)`, `Device(name, c_prefix="CONF_",
    c_storage="CONF_REG")`, `Outputs(reg_map, modbus, lebin_json, modbus_json, python)`,
    `GeneratorSettings(templates, outputs)`, `RegisterMap(device, generator, blocks:
    dict[str, Block])`.
  - `enum_numbers(values: list[EnumValue]) -> list[int]`.
  - `load_map_text(text: str, source: str = "<string>") -> RegisterMap` and
    `load_map(path: Path) -> RegisterMap`; both raise `MapError`.
  - `tests/helpers.py`: `EXAMPLE: Path`, `map_yaml(blocks, device="name: Dev", extra="") -> str`,
    `load(blocks, **kw) -> RegisterMap`, `errors(blocks, **kw) -> list[str]`.

- [ ] **Step 1: Write the failing tests**

Create `tests/helpers.py` with:

```python
"""Small builders for register maps used across the tests."""

import textwrap
from pathlib import Path

import pytest

from regmap.model import MapError, RegisterMap, load_map_text

EXAMPLE = Path(__file__).resolve().parents[1] / "example"


def map_yaml(blocks: str, device: str = "name: Dev", extra: str = "") -> str:
    """A complete map document; ``blocks`` is the YAML under ``blocks:`` (dedented)."""
    body = textwrap.indent(textwrap.dedent(blocks).strip("\n"), "  ")
    return f"device: {{{device}}}\n{textwrap.dedent(extra)}blocks:\n{body}\n"


def load(blocks: str, **kwargs: str) -> RegisterMap:
    return load_map_text(map_yaml(blocks, **kwargs))


def errors(blocks: str, **kwargs: str) -> list[str]:
    """Messages of the MapError raised while loading ``blocks``."""
    with pytest.raises(MapError) as exc:
        load(blocks, **kwargs)
    return exc.value.errors
```

Create `tests/test_model.py`:

```python
import pytest
from helpers import errors, load, map_yaml

from regmap.model import MapError, enum_numbers, load_map, load_map_text

ONE = "SYS: {{code: 0, registers: [{reg}]}}"


def reg_errors(reg: str) -> list[str]:
    return errors(ONE.format(reg=reg))


def test_minimal_map_loads_with_defaults():
    rmap = load(ONE.format(reg="{name: UPTIME, type: INT, access: RO, size: 4}"))
    assert rmap.device.name == "Dev"
    assert rmap.device.c_prefix == "CONF_"
    assert rmap.device.c_storage == "CONF_REG"
    assert rmap.generator.outputs.reg_map is None
    reg = rmap.blocks["SYS"].registers[0]
    assert (reg.name, reg.type, reg.access, reg.size) == ("UPTIME", "INT", "RO", 4)


def test_unknown_key_is_reported_with_register_name():
    assert reg_errors("{name: A, type: INT, access: RO, size: 4, defualt: 1}") == [
        "blocks.SYS.registers[0] (A).defualt: unknown key"
    ]


def test_duplicate_yaml_key_is_an_error():
    text = map_yaml("SYS:\n  code: 0\n  code: 1\n  registers: []")
    with pytest.raises(MapError) as exc:
        load_map_text(text)
    assert "found duplicate key 'code'" in exc.value.errors[0]


def test_yaml_syntax_error_names_the_source():
    with pytest.raises(MapError) as exc:
        load_map_text("device: [", source="map.yaml")
    assert exc.value.errors[0].startswith("map.yaml: ")


def test_top_level_must_be_a_mapping():
    with pytest.raises(MapError, match="top level must be a mapping"):
        load_map_text("- 1\n- 2\n")


def test_unquoted_boolean_text_gets_a_hint():
    msgs = reg_errors("{name: A, type: INT, access: RO, size: 4, label: ON}")
    assert msgs == [
        "blocks.SYS.registers[0] (A).label: YAML read this unquoted value as true/false;"
        " put the text in quotes"
    ]


def test_numeric_text_is_read_as_text():
    rmap = load(ONE.format(reg="{name: A, type: INT, access: RO, size: 4, label: 9600}"))
    assert rmap.blocks["SYS"].registers[0].label == "9600"


def test_label_must_be_single_line():
    assert (
        "must be a single line"
        in reg_errors('{name: A, type: INT, access: RO, size: 4, label: "a\\nb"}')[0]
    )


@pytest.mark.parametrize(
    ("reg", "message"),
    [
        (
            "{name: A, type: INT, access: RO, size: 3}",
            "size 3 is not allowed for INT; use 1, 2, 4, 8",
        ),
        ("{name: A, type: FLOAT, access: RO, size: 2}", "size 2 is not allowed for FLOAT; use 4"),
        ("{name: A, type: ENUM, access: RO, size: 8, values: [{name: V}]}", "not allowed for ENUM"),
        ("{name: A, type: INT, access: XX, size: 4}", "Input should be"),
        ("{name: 1A, type: INT, access: RO, size: 4}", "should match pattern"),
    ],
)
def test_register_field_rules(reg, message):
    assert message in reg_errors(reg)[0]


def test_string_accepts_any_size():
    load(ONE.format(reg="{name: S, type: STRING, access: RO, size: 13}"))


def test_enum_needs_values():
    assert reg_errors("{name: A, type: ENUM, access: RW, size: 1}") == [
        "blocks.SYS.registers[0] (A): ENUM register needs values"
    ]


@pytest.mark.parametrize(
    ("reg", "message"),
    [
        ("{name: A, type: INT, access: RW, size: 1, values: [{name: V}]}", "only allowed for ENUM"),
        ("{name: A, type: ENUM, access: RW, size: 1, values: []}", "at least one value"),
        (
            "{name: A, type: ENUM, access: RW, size: 1, values: [{name: V}, {name: V}]}",
            "duplicate value name V",
        ),
        (
            "{name: A, type: ENUM, access: RW, size: 1, values: [{name: V, value: 1}, {name: W, value: 1}]}",
            "duplicate enum value 1",
        ),
        (
            "{name: A, type: ENUM, access: RW, size: 1, values: [{name: V, value: 256}]}",
            "enum value 256 does not fit into 1 byte(s)",
        ),
        (
            "{name: A, type: ENUM, access: RW, size: 1, min: 0, values: [{name: V}]}",
            "min is derived",
        ),
    ],
)
def test_enum_rules(reg, message):
    assert message in " ".join(reg_errors(reg))


def test_enum_numbers_continue_after_explicit_value():
    rmap = load(
        ONE.format(
            reg="{name: A, type: ENUM, access: RW, size: 1, values: "
            "[{name: V0}, {name: V5, value: 5}, {name: V6}]}"
        )
    )
    assert enum_numbers(rmap.blocks["SYS"].registers[0].values) == [0, 5, 6]


@pytest.mark.parametrize(
    ("reg", "message"),
    [
        (
            "{name: A, type: INT, access: RO, size: 4, bits: [{name: B, bit: 0}]}",
            "only allowed for BIN",
        ),
        (
            "{name: A, type: BIN, access: RO, size: 1, bits: [{name: B, bit: 8}]}",
            "bit 8 (B) does not fit",
        ),
        (
            "{name: A, type: BIN, access: RO, size: 1, bits: [{name: B, bit: 1}, {name: C, bit: 1}]}",
            "bit 1 is defined twice",
        ),
        ("{name: A, type: BIN, access: RO, size: 1, bits: [{name: B}]}", "bit: Field required"),
    ],
)
def test_bit_rules(reg, message):
    assert message in " ".join(reg_errors(reg))


def test_bin_without_bits_is_fine():
    load(ONE.format(reg="{name: A, type: BIN, access: RO, size: 4}"))


@pytest.mark.parametrize(
    ("reg", "message"),
    [
        (
            "{name: A, type: ENUM, access: RW, size: 1, default: X, values: [{name: V}]}",
            "ENUM default must be one of its value names: V",
        ),
        (
            "{name: A, type: ENUM, access: RW, size: 1, default: 0, values: [{name: V}]}",
            "ENUM default",
        ),
        ("{name: A, type: STRING, access: RW, size: 3, default: abc}", "needs 4 bytes"),
        (
            "{name: A, type: STRING, access: RW, size: 4, default: 12}",
            "STRING default must be text",
        ),
        ("{name: A, type: INT, access: RW, size: 1, default: 256}", "does not fit into 1 unsigned"),
        ("{name: A, type: INT, access: RW, size: 1, default: -1}", "does not fit into 1 unsigned"),
        (
            "{name: A, type: INT, access: RW, size: 2, default: 1.5}",
            "INT default must be an integer",
        ),
        ("{name: A, type: INT, access: RW, size: 2, default: abc}", "INT default must be a number"),
        (
            "{name: A, type: INT, access: RW, size: 2, min: 5, default: 4}",
            "default 4 is below min 5",
        ),
        ("{name: A, type: FLOAT, access: RW, size: 4, max: 1, default: 1.5}", "above max 1"),
        ("{name: A, type: INT, access: RW, size: 2, min: 5, max: 1}", "max 1 is lower than min 5"),
    ],
)
def test_default_and_range_rules(reg, message):
    assert message in " ".join(reg_errors(reg))


def test_valid_defaults():
    rmap = load(
        """
        SYS:
          code: 0
          registers:
            - {name: E, type: ENUM, access: RW, size: 1, default: W, values: [{name: V}, {name: W}]}
            - {name: S, type: STRING, access: RW, size: 4, default: abc}
            - {name: F, type: FLOAT, access: RW, size: 4, min: -1, max: 2.5, default: -0.5}
            - {name: H, type: INT, access: RW, size: 2, max: 0x3FF, default: 0x10}
        """
    )
    assert [r.default for r in rmap.blocks["SYS"].registers] == ["W", "abc", -0.5, 16]


@pytest.mark.parametrize(
    ("modbus", "message"),
    [
        ("true", "Modbus is on by default"),
        ("{format: F32}", "format F32 is only allowed for FLOAT"),
        ("{address: 70000}", "less than or equal to 65535"),
        ("{adress: 1}", "modbus.adress: unknown key"),
    ],
)
def test_modbus_rules(modbus, message):
    assert message in " ".join(
        reg_errors(f"{{name: A, type: INT, access: RW, size: 2, modbus: {modbus}}}")
    )


def test_block_rules():
    assert "should match pattern" in errors("sys: {code: 0, registers: []}")[0]
    assert "less than or equal to 25" in errors("SYS: {code: 26, registers: []}")[0]


def test_device_name_must_be_an_identifier():
    assert (
        "should match pattern" in errors("SYS: {code: 0, registers: []}", device="name: my-dev")[0]
    )


def test_load_map_accepts_utf8_bom(tmp_path):
    path = tmp_path / "map.yaml"
    path.write_bytes(b"\xef\xbb\xbf" + map_yaml("SYS: {code: 0, registers: []}").encode())
    assert load_map(path).device.name == "Dev"
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `pytest -q tests/test_model.py`
Expected: FAIL — `ModuleNotFoundError: No module named 'regmap.model'`

- [ ] **Step 3: Implement**

Create `src/regmap/model.py` with:

```python
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
```

- [ ] **Step 4: Run the whole suite and the linters**

Run: `pytest -q && ruff check . && ruff format --check .`
Expected: `62 passed` (46 of them in this task's tests), `All checks passed!`, all files already formatted.

- [ ] **Step 5: Documentation**

Create `doc/yaml-format.md`:

````markdown
# YAML format

A register map is one YAML file. Only information that cannot be derived is written; names,
addresses, IDs, enum ranges and Modbus addresses are computed (see
[derived-values.md](derived-values.md)).

## Example

```yaml
device:
  name: Vms1511

generator:
  outputs:
    reg_map: ../Firmware/Vms1511-APP/Core/Common
    modbus: ../Firmware/Vms1511-APP/Core/Serial
    lebin_json: ../Tests/vms-1511
    modbus_json: ../Tests/vms-1511
    python: ../Tests/vms-1511

blocks:
  SYS:
    code: 0
    name: SYSTEM
    description: System MCU features, small BSP, RTOS etc.
    registers:
      - name: UPTIME
        type: INT
        access: RO
        size: 4
        label: Uptime
        default: 0
        unit: s
        description: Elapsed seconds from device startup

      - name: STATUS
        type: BIN
        access: RO
        size: 4
        label: Status register
        bits:
          - {name: STAT_BIT_ERROR,        bit: 0,  label: Generic error}
          - {name: STAT_BIT_CONFIG_FLASH, bit: 16, label: Configuration flash error}

  COM:
    code: 3
    registers:
      - name: MB_BAUD_RATE
        type: ENUM
        access: RWF
        size: 1
        default: MB_BAUD_19200
        values:
          - {name: MB_BAUD_9600,  label: "9600"}
          - {name: MB_BAUD_19200, label: "19200"}

      - name: MB_ADDRESS
        type: INT
        access: RO
        size: 2
        min: 32
        max: 63
        modbus: false
```

The complete VMS-1511 map is in [`example/vms1511.yaml`](../example/vms1511.yaml).

## `device`

| key | required | meaning |
|---|---|---|
| `name` | yes | Identifier used for output names: `<name>_registers.json`, `<name>_Modbus.json`, `<name>Regs.py`, Python class `<name>Regs`. |
| `c_prefix` | no, `CONF_` | Prefix of register ID macros (`CONF_SYS_UPTIME`) and access macros (`CONF_INT`, `CONF_PTR`, …). |
| `c_storage` | no, `CONF_REG` | Storage naming: `CONF_REG_BLOCK_NUMBER`, `CONF_REG_LIMIT`, `conf_reg_t`, `conf_reg_sys_t`. |

The storage variable is always called `conf`.

## `generator` (optional)

| key | meaning |
|---|---|
| `templates` | Directory with project templates, relative to the YAML file (see [templates.md](templates.md)). |
| `outputs.reg_map` | Directory for `reg_map.h` / `reg_map.c`. |
| `outputs.modbus` | Directory for `mb_rtu_app.h` / `mb_rtu_app.c`. |
| `outputs.lebin_json` | Directory for `<name>_registers.json`. |
| `outputs.modbus_json` | Directory for `<name>_Modbus.json`. |
| `outputs.python` | Directory for `<name>Regs.py`. |

Paths are relative to the YAML file; use forward slashes. A missing entry means the YAML
file's directory. `regmap generate --out DIR` ignores all of them.

## `blocks`

A mapping from block abbreviation (`^[A-Z][A-Z0-9_]*$`) to a block:

| key | required | rules |
|---|---|---|
| `code` | yes | 0–25, unique. Outputs are ordered by `code`, not by YAML order. |
| `name`, `description` | no | Documentation only. |
| `registers` | yes | List of registers; may be empty (the code is then treated as unused). |

## Register

| key | required | rules |
|---|---|---|
| `name` | yes | C identifier. The full name `<BLOCK>_<name>` must be unique (ignoring case). |
| `type` | yes | `BIN`, `INT`, `FLOAT`, `STRING`, `ENUM`. |
| `access` | yes | `RO`, `ROF` (flash, read-only), `RW`, `RWF` (flash), `RWIF` (internal flash, calibration). |
| `size` | yes | Bytes. `INT`/`BIN`: 1, 2, 4, 8. `FLOAT`: 4. `ENUM`: 1, 2, 4. `STRING`: any ≥ 1. |
| `address` | no | Byte offset in the block, default: end of the previous register. May leave a gap, must not overlap; the register must end within 4096 bytes. |
| `label` | no | Single line. |
| `description` | no | Text; use `\|-` for several lines. |
| `unit` | no | Text. |
| `default` | no | `INT`/`BIN`: integer that fits the size (unsigned). `FLOAT`: number. `ENUM`: the name of one of its values. `STRING`: text; its UTF-8 length + 1 must fit `size`. Must lie within `min`/`max` when given. |
| `min`, `max` | no | Numbers (`0x…` allowed), `min ≤ max`. Not allowed for `ENUM` (derived from the values). |
| `modbus` | no | Omitted: exposed with an automatic address. `false`: not exposed. `{address: N}`: fixed start address in its space. `{format: F32}`: `FLOAT` as an IEEE float in two registers (default: int16 × 10). |
| `values` | `ENUM` only, required | List of `{name, value?, label?, description?}`. `value` defaults to previous + 1 (first 0). Names and values unique; values must fit the size. |
| `bits` | `BIN` only, optional | List of `{name, bit, label?, description?}`. `bit` is required, `0 ≤ bit < 8·size`, unique. |

Further rules:

- Bit names and enum value names are unique across the whole map (they become C macros and
  enum constants).
- Unknown keys and duplicate keys are errors.
- Text fields accept plain numbers (`label: 9600` means `"9600"`). YAML reads unquoted
  `ON`, `OFF`, `YES`, `NO`, `true`, `false` as booleans; quote such texts.

## Error messages

All problems are reported at once, each with its location and the register name, e.g.

```
error: vms1511.yaml: 2 problem(s)
  blocks.COM.registers[1] (MB_PARITY).default: ENUM default must be one of its value names: MB_PARITY_NONE, MB_PARITY_EVEN, MB_PARITY_ODD
  blocks.SYS.registers[2] (TEST).address: 10 overlaps the previous register ending at 12
```

## Editor support

`regmap schema -o regmap.schema.json` writes a JSON Schema of this format. With the VS Code
YAML extension, add this first line to the map for completion and validation while typing:

```yaml
# yaml-language-server: $schema=regmap.schema.json
```

The importer does not add this line.
````

- [ ] **Step 6: Commit**

```bash
git add src/regmap/model.py tests/helpers.py doc/yaml-format.md tests/test_model.py
git commit -m "feat: YAML register map model with validation"
```

### Task 3: Derived values (resolve)

**Files:**
- Create: `src/regmap/resolve.py`
- Modify: `tests/helpers.py`
- Create: `doc/derived-values.md`
- Test: `tests/test_resolve.py`

**Interfaces:**
- Consumes: `regmap.codes`, `regmap.model` (`RegisterMap`, `Register`, `Block`,
  `ModbusOptions`, `MapError`, `enum_numbers`).
- Produces (`regmap.resolve`), all frozen dataclasses:
  - `Item(name: str, value: int, label: str, description: str)` — enum value or bit.
  - `ModbusPlacement(space: str, addresses: tuple[int, ...], format: str, float_x10: bool)`.
  - `ResolvedRegister(block, block_code, name, full_name, type, access, size, address, id,
    label, description, unit, default, min, max, range_min, range_max, values, bits, modbus,
    c_type, c_member, c_array)` with property `items` (values for ENUM, else bits).
  - `ResolvedBlock(abbrev, code, registers, end)`.
  - `ResolvedMap(name, c_prefix, c_storage, blocks, flash, calib_serial, calib, input_last,
    hold_last)` with property `registers` (iterator in output order) and
    `block_by_code(code) -> ResolvedBlock | None`.
  - `resolve(rmap: RegisterMap) -> ResolvedMap` (raises `MapError`).
  - `tests/helpers.py` gains `resolved(blocks, **kw) -> ResolvedMap`; `errors()` now also
    resolves.

- [ ] **Step 1: Write the failing tests**

Replace `tests/helpers.py` with:

```python
"""Small builders for register maps used across the tests."""

import textwrap
from pathlib import Path

import pytest

from regmap.model import MapError, RegisterMap, load_map_text
from regmap.resolve import ResolvedMap, resolve

EXAMPLE = Path(__file__).resolve().parents[1] / "example"


def map_yaml(blocks: str, device: str = "name: Dev", extra: str = "") -> str:
    """A complete map document; ``blocks`` is the YAML under ``blocks:`` (dedented)."""
    body = textwrap.indent(textwrap.dedent(blocks).strip("\n"), "  ")
    return f"device: {{{device}}}\n{textwrap.dedent(extra)}blocks:\n{body}\n"


def load(blocks: str, **kwargs: str) -> RegisterMap:
    return load_map_text(map_yaml(blocks, **kwargs))


def resolved(blocks: str, **kwargs: str) -> ResolvedMap:
    return resolve(load(blocks, **kwargs))


def errors(blocks: str, **kwargs: str) -> list[str]:
    """Messages of the MapError raised while loading or resolving ``blocks``."""
    with pytest.raises(MapError) as exc:
        resolve(load(blocks, **kwargs))
    return exc.value.errors
```

Create `tests/test_resolve.py`:

```python
import pytest
from helpers import errors, resolved

from regmap.codes import RegType

SYS = """
SYS:
  code: 0
  registers:
    - {name: UPTIME, type: INT, access: RO, size: 4}
    - {name: FLAGS, type: BIN, access: RO, size: 2, bits: [{name: F_A, bit: 0}]}
    - {name: GAP, type: INT, access: RW, size: 1, address: 10}
    - {name: TAIL, type: INT, access: RW, size: 2}
"""


def test_addresses_follow_previous_register_and_allow_gaps():
    block = resolved(SYS).blocks[0]
    assert [(r.full_name, r.address) for r in block.registers] == [
        ("SYS_UPTIME", 0),
        ("SYS_FLAGS", 4),
        ("SYS_GAP", 10),
        ("SYS_TAIL", 11),
    ]
    assert block.end == 13


def test_ids_are_composed_from_resolved_addresses():
    regs = list(resolved(SYS).registers)
    assert [f"0x{r.id:08X}" for r in regs] == [
        "0x00000112",
        "0x00004011",
        "0x0000A150",
        "0x0000B151",
    ]


def test_overlap_and_block_overflow_are_errors():
    msgs = errors(
        """
        SYS:
          code: 0
          registers:
            - {name: A, type: INT, access: RO, size: 4}
            - {name: B, type: INT, access: RO, size: 4, address: 2}
            - {name: C, type: STRING, access: RO, size: 10, address: 4090}
        """
    )
    assert msgs == [
        "blocks.SYS.registers[1] (B).address: 2 overlaps the previous register ending at 4",
        "blocks.SYS.registers[2] (C): ends at 4100, beyond the 4096-byte block",
    ]


def test_blocks_are_ordered_by_code_not_yaml_order():
    rmap = resolved(
        """
        COM: {code: 3, registers: [{name: A, type: INT, access: RO, size: 2}]}
        SYS: {code: 0, registers: [{name: B, type: INT, access: RO, size: 2}]}
        """
    )
    assert [b.abbrev for b in rmap.blocks] == ["SYS", "COM"]
    assert rmap.block_by_code(3).abbrev == "COM"
    assert rmap.block_by_code(7) is None


def test_duplicate_block_code_is_an_error():
    assert errors("A: {code: 1, registers: []}\nB: {code: 1, registers: []}") == [
        "blocks.B.code: 1 is already used by A"
    ]


def test_full_names_must_be_unique_ignoring_case():
    msgs = errors(
        """
        SYS:
          code: 0
          registers:
            - {name: MODE, type: INT, access: RO, size: 2}
            - {name: Mode, type: INT, access: RO, size: 2}
        """
    )
    assert msgs == [
        "blocks.SYS.registers[1] (Mode).name: name SYS_Mode is already used at"
        " blocks.SYS.registers[0] (MODE).name"
    ]


def test_bit_and_enum_names_are_unique_across_the_map():
    msgs = errors(
        """
        SYS:
          code: 0
          registers:
            - {name: A, type: BIN, access: RO, size: 1, bits: [{name: SAME, bit: 0}]}
            - {name: B, type: ENUM, access: RW, size: 1, values: [{name: SAME}]}
        """
    )
    assert msgs == [
        "blocks.SYS.registers[1] (B).values[0]: name SAME is already used at"
        " blocks.SYS.registers[0] (A).bits[0]"
    ]


def test_enum_default_and_range_are_resolved_to_numbers():
    reg = next(
        resolved(
            """
            SYS:
              code: 0
              registers:
                - {name: E, type: ENUM, access: RW, size: 1, default: C, values: [{name: A, value: 2}, {name: B}, {name: C, value: 7}]}
            """
        ).registers
    )
    assert [(v.name, v.value) for v in reg.values] == [("A", 2), ("B", 3), ("C", 7)]
    assert (reg.default, reg.min, reg.max, reg.range_min, reg.range_max) == (7, None, None, 2, 7)


def test_c_types():
    rmap = resolved(
        """
        COM:
          code: 3
          registers:
            - {name: MB_BAUD_RATE, type: ENUM, access: RW, size: 1, values: [{name: B9600}]}
            - {name: GAIN, type: FLOAT, access: RW, size: 4}
            - {name: B1, type: INT, access: RW, size: 1}
            - {name: B8, type: INT, access: RW, size: 8}
            - {name: TXT4, type: STRING, access: RW, size: 4}
            - {name: TXT16, type: STRING, access: RW, size: 16}
            - {name: TXT6, type: STRING, access: RW, size: 6}
        """
    )
    assert [(r.c_type, r.c_member, r.c_array) for r in rmap.registers] == [
        ("com_mb_baud_rate_t", "mb_baud_rate", None),
        ("float", "gain", None),
        ("uint8_t", "b1", None),
        ("uint64_t", "b8", None),
        ("uint32_t", "txt4", None),
        ("uint8_t", "txt16", 16),
        ("uint8_t", "txt6", 6),
    ]


MODBUS = """
SYS:
  code: 0
  registers:
    - {name: I4, type: INT, access: RO, size: 4}
    - {name: H1, type: INT, access: RW, size: 1}
    - {name: H5, type: STRING, access: RW, size: 5}
    - {name: E, type: ENUM, access: RWF, size: 1, values: [{name: E0}]}
    - {name: F, type: FLOAT, access: RW, size: 4}
    - {name: F32, type: FLOAT, access: RW, size: 4, modbus: {format: F32}}
    - {name: HIDDEN, type: INT, access: RW, size: 2, modbus: false}
    - {name: FIXED, type: INT, access: ROF, size: 2, modbus: {address: 100}}
    - {name: NEXT, type: INT, access: RO, size: 2}
"""


def test_modbus_allocation():
    rmap = resolved(MODBUS)
    placed = {
        r.name: r.modbus and (r.modbus.space, r.modbus.addresses, r.modbus.format)
        for r in rmap.registers
    }
    assert placed == {
        "I4": ("INPUT", (0, 1), "INT"),
        "H1": ("HOLD", (0,), "INT"),
        "H5": ("HOLD", (1, 2, 3), "STRING"),
        "E": ("HOLD", (4,), "ENUM"),
        "F": ("HOLD", (5,), "FLOAT"),
        "F32": ("HOLD", (6, 7), "FLOAT32"),
        "HIDDEN": None,
        "FIXED": ("INPUT", (100,), "INT"),
        "NEXT": ("INPUT", (101,), "INT"),
    }
    assert [r.modbus.float_x10 for r in rmap.registers if r.type is RegType.FLOAT] == [True, False]
    assert (rmap.input_last, rmap.hold_last) == (101, 7)


def test_empty_modbus_space_has_last_minus_one():
    rmap = resolved("SYS: {code: 0, registers: [{name: A, type: INT, access: RO, size: 2}]}")
    assert (rmap.input_last, rmap.hold_last) == (0, -1)


def test_modbus_address_collision_is_an_error():
    msgs = errors(
        """
        SYS:
          code: 0
          registers:
            - {name: A, type: INT, access: RW, size: 4}
            - {name: B, type: INT, access: RW, size: 2, modbus: {address: 1}}
        """
    )
    assert msgs == ["blocks.SYS.registers[1] (B).modbus: HOLD address 1 is already used by SYS_A"]


def test_modbus_address_beyond_range_is_an_error():
    msgs = errors(
        "SYS: {code: 0, registers: [{name: A, type: INT, access: RW, size: 4, modbus: {address: 65535}}]}"
    )
    assert msgs == ["blocks.SYS.registers[0] (A).modbus: address 65536 is beyond 65535"]


FLASH_CALIB = """
FACT:
  code: 1
  registers:
    - {name: SERIAL_NUMBER, type: INT, access: RO, size: 4}
    - {name: VERSION, type: INT, access: ROF, size: 4}
CALIB:
  code: 4
  registers:
    - {name: GAIN, type: FLOAT, access: RWIF, size: 4}
    - {name: MODE, type: ENUM, access: RWF, size: 1, values: [{name: M0}]}
"""


def test_flash_and_calib_lists():
    rmap = resolved(FLASH_CALIB)
    assert [r.full_name for r in rmap.flash] == ["FACT_VERSION", "CALIB_MODE"]
    assert rmap.calib_serial.full_name == "FACT_SERIAL_NUMBER"
    assert [r.full_name for r in rmap.calib] == ["CALIB_GAIN"]


def test_calib_serial_is_optional():
    rmap = resolved("SYS: {code: 0, registers: [{name: A, type: INT, access: RWIF, size: 4}]}")
    assert rmap.calib_serial is None
    assert [r.full_name for r in rmap.calib] == ["SYS_A"]


@pytest.mark.parametrize("access", ["RWIF"])
def test_serial_number_is_not_listed_twice(access):
    rmap = resolved(
        f"FACT: {{code: 1, registers: [{{name: SERIAL_NUMBER, type: INT, access: {access}, size: 4}}]}}"
    )
    assert rmap.calib_serial is not None
    assert rmap.calib == ()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `pytest -q tests/test_resolve.py`
Expected: FAIL — `ModuleNotFoundError: No module named 'regmap.resolve'`

- [ ] **Step 3: Implement**

Create `src/regmap/resolve.py` with:

```python
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
```

- [ ] **Step 4: Run the whole suite and the linters**

Run: `pytest -q && ruff check . && ruff format --check .`
Expected: `78 passed` (16 of them in this task's tests), `All checks passed!`, all files already formatted.

- [ ] **Step 5: Documentation**

Create `doc/derived-values.md`:

````markdown
# Derived values

Everything below is computed by `regmap` (module `resolve.py`) and must not be written into
the YAML map.

## Names

- Full name: `<BLOCK>_<name>`, e.g. `SYS` + `UPTIME` → `SYS_UPTIME`.
- C ID macro: `<c_prefix><full name>`, e.g. `CONF_SYS_UPTIME`.
- C struct member: the register `name` in lower case, e.g. `conf.sys.uptime`.

## Address

The byte offset of a register in its block is the end of the previous register (the first
register starts at 0), unless `address` is given. Gaps become `uint8_t reserved<N>[<gap>]`
members of the block structure.

## Register ID

```
bits 31..24  block code
bits 23..12  address in the block
bits 11..8   type code     BIN 0, INT 1, FLOAT 2, STRING 3, ENUM 5
bits  7..4   1 + 2 * access code   RO 0, ROF 1, RW 2, RWF 3, RWIF 3
bits  3..0   floor(log2(size))
```

Example: `SYS_IO_INPUT` (block 0, address 20, INT, RO, 2 bytes) → `0x00014111`.

Because the ID contains the address, inserting a register in the middle of a block changes
the IDs of all following registers. `regmap generate` detects this (see
[stability-check.md](stability-check.md)).

## C types

| register | C member |
|---|---|
| `ENUM` | `<full name lower>_t` (a generated `typedef enum`) |
| `FLOAT` | `float` |
| size 1 / 2 / 4 / 8 | `uint8_t` / `uint16_t` / `uint32_t` / `uint64_t` |
| any other size | `uint8_t <member>[<size>]` |

Block size (`<c_storage>_LIMIT`) is the end of the last register. `CONF_DIM_CONDITION` checks
`sizeof` of each block structure against that size rounded up to a multiple of 4.

## Enum range

For `ENUM` registers `min` and `max` are the smallest and largest value.

## Modbus

- Space: `RO` and `ROF` registers are input registers, all others holding registers.
- Words per register: `INT`/`BIN`/`STRING` → `ceil(size / 2)`, `ENUM` → 1, `FLOAT` → 1
  (value × 10 as int16) or 2 with `format: F32`.
- Allocation: registers in output order (block code, then YAML order). Each space has a
  counter starting at 0. `modbus: {address: N}` sets the counter before the register; the
  register takes its words consecutively and the counter continues after them.
- An address may be used only once per space.
- `MB_*_FIRST` is 0; `MB_*_LAST` is the highest allocated address (−1 for an empty space).

## Flash and calibration lists

- Flash list (`<c_storage>_FLASH`): registers with access `RWF` or `ROF`.
  `FLASH_LENGTH` = Σ (size + 4).
- Calibration list (`<c_storage>_CALIB`): `FACT_SERIAL_NUMBER` first when the map has it
  (it counts 8 bytes), then all `RWIF` registers. `CALIB_LENGTH` = 8 + Σ (size + 4).
- Logger and synced lists are always empty (kept for the firmware API).
- The storage always has 26 block slots.
````

- [ ] **Step 6: Commit**

```bash
git add src/regmap/resolve.py tests/helpers.py doc/derived-values.md tests/test_resolve.py
git commit -m "feat: resolve addresses, IDs, C types and Modbus allocation"
```

### Task 4: Templates and placeholder filling

**Files:**
- Create: `src/regmap/templating.py`
- Move: `template/*` → `src/regmap/templates/`
- Create: `doc/templates.md`
- Test: `tests/test_templating.py`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces (`regmap.templating`):
  - `class TemplateError(Exception)`.
  - `read_text_file(path: Path) -> str` — UTF-8 (BOM tolerated), falls back to cp1250.
  - `load_template(name: str, project_dir: Path | None = None) -> str` — `\n` line endings,
    final newline; project file wins over the packaged default.
  - `fill(template: str, fragments: dict[str, str], name: str) -> str` — raises
    `TemplateError` when a placeholder is missing.
  - Package data `src/regmap/templates/{reg_map_temp.h, reg_map_temp.c, mb_rtu_app_temp.h,
    mb_rtu_app_temp.c}`.

- [ ] **Step 1: Move the templates into the package**

The four templates in the untracked top-level `template/` folder become package data,
unchanged (their CRLF line endings are kept by `.gitattributes`):

```bash
mkdir -p src/regmap/templates
mv template/reg_map_temp.h template/reg_map_temp.c template/mb_rtu_app_temp.h template/mb_rtu_app_temp.c src/regmap/templates/
rmdir template
```

- [ ] **Step 2: Write the failing tests**

Create `tests/test_templating.py`:

```python
import pytest

from regmap.templating import TemplateError, fill, load_template, read_text_file

TEMPLATES = ("reg_map_temp.h", "reg_map_temp.c", "mb_rtu_app_temp.h", "mb_rtu_app_temp.c")


@pytest.mark.parametrize("name", TEMPLATES)
def test_default_templates_are_packaged_and_normalized(name):
    text = load_template(name)
    assert "\r" not in text
    assert text.endswith("\n")
    assert "/* < " in text


def test_project_template_overrides_default(tmp_path):
    (tmp_path / "reg_map_temp.h").write_bytes(b"custom\r\n/* < DEFINE REG MAP > */")
    assert load_template("reg_map_temp.h", tmp_path) == "custom\n/* < DEFINE REG MAP > */\n"
    assert load_template("reg_map_temp.c", tmp_path) == load_template("reg_map_temp.c")


def test_legacy_cp1250_file_is_readable(tmp_path):
    path = tmp_path / "t.h"
    path.write_bytes("// Žluťoučký kůň\n".encode("cp1250"))
    assert read_text_file(path) == "// Žluťoučký kůň\n"


def test_fill_keeps_the_placeholder_line_ending():
    template = "top\n/* < X > */\nbottom\n"
    assert fill(template, {"/* < X > */": "a\nb\n"}, "t") == "top\na\nb\n\nbottom\n"
    assert fill(template, {"/* < X > */": ""}, "t") == "top\n\nbottom\n"


def test_missing_placeholder_is_an_error():
    with pytest.raises(TemplateError, match=r"t\.h: placeholder /\* < Y > \*/ not found"):
        fill("/* < X > */\n", {"/* < X > */": "", "/* < Y > */": ""}, "t.h")
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `pytest -q tests/test_templating.py`
Expected: FAIL — `ModuleNotFoundError: No module named 'regmap.templating'`

- [ ] **Step 4: Implement**

Create `src/regmap/templating.py` with:

```python
"""C templates: lookup (project directory first, then package defaults) and placeholder filling."""

from importlib.resources import files
from pathlib import Path


class TemplateError(Exception):
    pass


def read_text_file(path: Path) -> str:
    """Read UTF-8 (with or without BOM), falling back to cp1250 for legacy files."""
    data = path.read_bytes()
    try:
        return data.decode("utf-8-sig")
    except UnicodeDecodeError:
        return data.decode("cp1250")


def load_template(name: str, project_dir: Path | None = None) -> str:
    """Template text with ``\\n`` line endings and a final newline."""
    if project_dir is not None and (project_dir / name).is_file():
        text = read_text_file(project_dir / name)
    else:
        text = files("regmap").joinpath("templates", name).read_text(encoding="utf-8")
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    return text if text.endswith("\n") else text + "\n"


def fill(template: str, fragments: dict[str, str], name: str) -> str:
    """Replace every placeholder by its fragment; a missing placeholder is an error."""
    missing = [p for p in fragments if p not in template]
    if missing:
        raise TemplateError(f"{name}: placeholder {missing[0]} not found in template")
    for placeholder, text in fragments.items():
        template = template.replace(placeholder, text)
    return template
```

- [ ] **Step 5: Run the whole suite and the linters**

Run: `pytest -q && ruff check . && ruff format --check .`
Expected: `86 passed` (8 of them in this task's tests), `All checks passed!`, all files already formatted.

- [ ] **Step 6: Documentation**

Create `doc/templates.md`:

````markdown
# Templates

The C outputs are produced by filling placeholders in templates. The package ships default
templates (`src/regmap/templates/`); a project can replace any of them.

| template | output | placeholders |
|---|---|---|
| `reg_map_temp.h` | `reg_map.h` | `/* < DEFINE REG MAP > */`, `/* < DEFINE REG BITS > */`, `/* < REG MAP PARAMS > */`, `/* < REG MAP TYPEDEFS > */` |
| `reg_map_temp.c` | `reg_map.c` | `/* < DEFINE REG MAP STORAGE > */`, `/* < REG MAP FACTORY > */` |
| `mb_rtu_app_temp.h` | `mb_rtu_app.h` | `/* < MODBUS INPUT DEFINE > */`, `/* < MODBUS HOLD DEFINE > */` |
| `mb_rtu_app_temp.c` | `mb_rtu_app.c` | `/* < READ INPUT REG > */`, `/* < READ HOLD REG > */`, `/* < WRITE HOLD REG > */` |

## Project templates

Point `generator.templates` to a directory (relative to the YAML file):

```yaml
generator:
  templates: templates
```

A file there with one of the names above is used instead of the default; missing files fall
back to the defaults, so a project overrides only what it needs.

Rules for custom templates:

- Every placeholder of the template must be present exactly as written above; a missing
  placeholder stops the generation with an error.
- The generated text replaces the placeholder; the rest of the line and all other text stay
  unchanged. Each generated block ends with a newline, so the placeholder's own line end
  becomes an empty line.
- Templates may use LF or CRLF line endings and UTF-8 (with or without BOM) or cp1250; outputs
  are always UTF-8 with CRLF.
````

- [ ] **Step 7: Commit**

```bash
git add src/regmap/templating.py doc/templates.md tests/test_templating.py src/regmap/templates
git commit -m "feat: packaged C templates with project override"
```

### Task 5: reg_map.h / reg_map.c generator

**Files:**
- Create: `src/regmap/generators/__init__.py`
- Create: `src/regmap/generators/c_regmap.py`
- Create: `doc/outputs.md`
- Test: `tests/test_c_regmap.py`

**Interfaces:**
- Consumes: `regmap.resolve` (`ResolvedMap`, `ResolvedBlock`, `ResolvedRegister`, `Item`),
  `regmap.codes` (`BLOCK_COUNT`, `RegType`).
- Produces:
  - `regmap.generators`: `name_width(rmap) -> int`, `fragment(lines: list[str]) -> str`,
    `crlf(text) -> str`, `describe(reg, item_prefix: Callable[[Item], str]) -> str` (used by
    Task 7).
  - `regmap.generators.c_regmap`: placeholder constants `REG_MAP`, `REG_BITS`, `REG_PARAMS`,
    `REG_TYPEDEFS`, `REG_STORAGE`, `REG_FACTORY`; `header_fragments(rmap) -> dict[str, str]`
    (for `reg_map_temp.h`), `source_fragments(rmap) -> dict[str, str]` (for
    `reg_map_temp.c`), `struct_name(rmap, block) -> str`, `c_float(value) -> str`,
    `c_string(text) -> str`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_c_regmap.py`:

```python
from helpers import resolved

from regmap.generators import name_width
from regmap.generators.c_regmap import (
    REG_BITS,
    REG_FACTORY,
    REG_MAP,
    REG_PARAMS,
    REG_STORAGE,
    REG_TYPEDEFS,
    c_float,
    c_string,
    header_fragments,
    source_fragments,
)

MAP = """
SYS:
  code: 0
  registers:
    - {name: UPTIME, type: INT, access: RO, size: 4, label: Uptime, default: 0}
    - {name: STATUS, type: BIN, access: RO, size: 4, bits: [{name: stat_err, bit: 0}, {name: STAT_A_VERY_LONG_BIT_NAME_X, bit: 17}]}
FACT:
  code: 1
  registers:
    - {name: SERIAL_NUMBER, type: INT, access: RO, size: 4}
COM:
  code: 3
  registers:
    - {name: MODE, type: ENUM, access: RWF, size: 1, default: M_B, values: [{name: M_A}, {name: M_B, value: 5}]}
    - {name: NAME, type: STRING, access: RWIF, size: 6, address: 2, default: ab"c}
    - {name: GAIN, type: FLOAT, access: RWF, size: 4, default: 2}
    - {name: SHORT, type: INT, access: RW, size: 2, default: 7}
"""


def test_name_width_counts_registers_bits_and_enum_values():
    assert name_width(resolved(MAP)) == len("STAT_A_VERY_LONG_BIT_NAME_X")


def test_define_map():
    lines = header_fragments(resolved(MAP))[REG_MAP].splitlines()
    # width 27 (longest bit name): 1 + 27 - len("SYS_UPTIME") = 18 spaces
    assert lines[0] == "#define CONF_SYS_UPTIME" + " " * 18 + "0x00000112u  ///< Uptime"
    assert lines[1] == "#define CONF_SYS_STATUS" + " " * 18 + "0x00004012u  ///< "
    assert len(lines) == 7


def test_define_bits_are_upper_case_with_min_three_spaces():
    assert header_fragments(resolved(MAP))[REG_BITS] == (
        "#define STAT_ERR                      (1 << (0))\n"
        "#define STAT_A_VERY_LONG_BIT_NAME_X   (1 << (17))\n"
    )


def test_params():
    lines = header_fragments(resolved(MAP))[REG_PARAMS].splitlines()
    assert lines[2] == "#define CONF_REG_CALIB_NUMBER      (2)"  # serial number + NAME
    assert lines[4] == "#define CONF_REG_FLASH_NUMBER      (2)"  # MODE, GAIN
    assert lines[6] == "#define CONF_REG_CALIB_LENGTH      (18)"  # 8 + (6 + 4)
    assert lines[8] == "#define CONF_REG_FLASH_LENGTH      (13)"  # (1 + 4) + (4 + 4)
    assert lines[10] == ""
    assert lines[11] == (
        "#define CONF_DIM_CONDITION ((sizeof(conf_reg_sys_t) != 8) || "
        "(sizeof(conf_reg_fact_t) != 4) || (sizeof(conf_reg_com_t) != 16) || 0)"
    )


def test_typedefs():
    text = header_fragments(resolved(MAP))[REG_TYPEDEFS]
    assert text.startswith("\ntypedef enum\n{\n  M_A = 0,\n  M_B = 5,\n}com_mode_t ;\n\n")
    assert (
        "typedef struct __packed __aligned(4)\n{\n"
        "  com_mode_t mode;\n"
        "  uint8_t reserved0[1];\n"
        "  uint8_t name[6];\n"
        "  float gain;\n"
        "  uint16_t short;\n"
        "}conf_reg_com_t;\n\n"
    ) in text
    assert (
        "\n\ntypedef struct \n{\n  conf_reg_sys_t sys;\n  conf_reg_fact_t fact;\n  uint32_t res3;\n"
        in text
    )
    assert text.endswith("  uint32_t res26;\n}\nconf_reg_t;\n\n")


def test_custom_storage_name():
    rmap = resolved(
        "SYS: {code: 0, registers: [{name: A, type: INT, access: RO, size: 4}]}",
        device="name: Dev, c_storage: MY_REG, c_prefix: REG_",
    )
    header = header_fragments(rmap)
    assert "sizeof(my_reg_sys_t)" in header[REG_PARAMS]
    assert header[REG_TYPEDEFS].endswith("}\nmy_reg_t;\n\n")
    assert header[REG_MAP].startswith("#define REG_SYS_A ")
    assert source_fragments(rmap)[REG_STORAGE].startswith("my_reg_t conf;\n\n\n")


def test_storage_arrays():
    lines = source_fragments(resolved(MAP))[REG_STORAGE].splitlines()
    assert lines[:3] == ["conf_reg_t conf;", "", ""]
    assert lines[3] == (
        "uint8_t* const CONF_REG[CONF_REG_BLOCK_NUMBER] = {(uint8_t*)&conf.sys, "
        "(uint8_t*)&conf.fact, NULL, (uint8_t*)&conf.com, " + "NULL, " * 22 + "};"
    )
    assert lines[5:7] == [
        "const uint32_t CONF_REG_LIMIT[CONF_REG_BLOCK_NUMBER] = {",
        "8, 4, 0, 14, " + "0, " * 22 + "};",
    ]
    assert lines[8:10] == [
        "const uint32_t CONF_REG_FLASH[CONF_REG_FLASH_NUMBER] = {",
        "CONF_COM_MODE, CONF_COM_GAIN, };",
    ]
    assert lines[11:13] == ["const uint32_t CONF_REG_LOGGER[CONF_REG_LOGGER_NUMBER] = {", "};"]
    assert lines[15] == "CONF_FACT_SERIAL_NUMBER, CONF_COM_NAME, };"
    assert lines[-3:] == ["const uint32_t CONF_REG_SYNCED[CONF_REG_SYNCED_NUMBER] = {", "};", ""]


def test_factory_values():
    assert source_fragments(resolved(MAP))[REG_FACTORY] == (
        "  CONF_INT(CONF_SYS_UPTIME)                    = 0;\n"
        "  CONF_BYTE(CONF_COM_MODE)                     = 5;\n"
        '  memcpy(CONF_PTR(CONF_COM_NAME), "ab\\"c", sizeof("ab\\"c"));\n'
        "  CONF_FLOAT(CONF_COM_GAIN)                    = 2.0;\n"
        "  CONF_SHORT(CONF_COM_SHORT)                   = 7;\n"
    )


def test_c_float_and_c_string():
    assert [c_float(v) for v in (2, 1.25, 0.1234567, -0.5, 1e-9)] == [
        "2.0", "1.25", "0.123457", "-0.5", "0.0",
    ]  # fmt: skip
    assert c_string('a\\b"\n') == '"a\\\\b\\"\\n"'


def test_block_without_registers_is_an_unused_slot():
    rmap = resolved(
        "SYS: {code: 0, registers: []}\nCOM: {code: 3, registers: [{name: A, type: INT, access: RO, size: 2}]}"
    )
    header = header_fragments(rmap)
    assert "conf_reg_sys_t" not in header[REG_PARAMS] + header[REG_TYPEDEFS]
    assert "  uint32_t res1;\n" in header[REG_TYPEDEFS]
    storage = source_fragments(rmap)[REG_STORAGE].splitlines()
    assert storage[3].startswith(
        "uint8_t* const CONF_REG[CONF_REG_BLOCK_NUMBER] = {NULL, NULL, NULL, (uint8_t*)&conf.com, "
    )
    assert storage[6].startswith("0, 0, 0, 2, 0, ")
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `pytest -q tests/test_c_regmap.py`
Expected: FAIL — `ModuleNotFoundError: No module named 'regmap.generators'`

- [ ] **Step 3: Implement**

Create `src/regmap/generators/__init__.py` with:

```python
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
```

Create `src/regmap/generators/c_regmap.py` with:

```python
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
```

- [ ] **Step 4: Run the whole suite and the linters**

Run: `pytest -q && ruff check . && ruff format --check .`
Expected: `96 passed` (10 of them in this task's tests), `All checks passed!`, all files already formatted.

- [ ] **Step 5: Documentation**

Create `doc/outputs.md`:

```markdown
# Outputs

`regmap generate` writes seven files. All of them are UTF-8 without BOM with CRLF line
endings. The C files and the Python file of VMS-1511 are byte-identical to the former VBA
outputs; the JSON files differ only as listed in
[differences-from-vba.md](differences-from-vba.md).

| output key | files |
|---|---|
| `reg_map` | `reg_map.h`, `reg_map.c` |
| `modbus` | `mb_rtu_app.h`, `mb_rtu_app.c` |
| `lebin_json` | `<name>_registers.json` |
| `modbus_json` | `<name>_Modbus.json` |
| `python` | `<name>Regs.py` |

Blocks appear in `code` order, registers in YAML order. Column alignment in the C files uses
the width `W` = the longest name among all register full names, bit names and enum value
names.

## `reg_map.h`

- **DEFINE REG MAP** — one ID macro per register:
  `#define CONF_SYS_UPTIME            0x00000112u  ///< Uptime` (name padded to `W + 1`).
- **DEFINE REG BITS** — one macro per bit, name in upper case:
  `#define STAT_BIT_ERROR                (1 << (0))` (name padded to 30, at least 3 spaces).
- **REG MAP PARAMS** — `<c_storage>_BLOCK_NUMBER (26)`, `_LOGGER_NUMBER`, `_CALIB_NUMBER`,
  `_SYNCED_NUMBER`, `_FLASH_NUMBER`, `_LOGGER_LENGTH`, `_CALIB_LENGTH`, `_SYNCED_LENGTH`,
  `_FLASH_LENGTH`, `_LOCAL_LENGTH` and `CONF_DIM_CONDITION`, which compares `sizeof` of every
  block structure with its size.
- **REG MAP TYPEDEFS** — a `typedef enum` per ENUM register, a
  `typedef struct __packed __aligned(4)` per block and the storage structure
  `<c_storage lower>_t` with 26 slots (`uint32_t res<N>` for unused codes).

## `reg_map.c`

- **DEFINE REG MAP STORAGE** — the storage variable `conf`, the block pointer array
  `<c_storage>[]`, `_LIMIT[]` (block sizes) and the `_FLASH[]`, `_LOGGER[]`, `_CALIB[]`,
  `_SYNCED[]` ID lists.
- **REG MAP FACTORY** — one line per register with a `default`:
  `CONF_BYTE/SHORT/INT(...) = n;` by size, `CONF_FLOAT(...) = 2.0;` (1–6 decimals),
  `memcpy(CONF_PTR(...), "text", sizeof("text"));` for strings. ENUM defaults are written as
  numbers.
```

- [ ] **Step 6: Commit**

```bash
git add src/regmap/generators/__init__.py src/regmap/generators/c_regmap.py doc/outputs.md tests/test_c_regmap.py
git commit -m "feat: reg_map.h/.c fragments"
```

### Task 6: mb_rtu_app.h / mb_rtu_app.c generator

**Files:**
- Create: `src/regmap/generators/c_modbus.py`
- Modify: `doc/outputs.md`
- Test: `tests/test_c_modbus.py`

**Interfaces:**
- Consumes: `regmap.generators` (`fragment`, `name_width`), `regmap.resolve`.
- Produces (`regmap.generators.c_modbus`): placeholder constants `INPUT_DEFINE`,
  `HOLD_DEFINE`, `READ_INPUT`, `READ_HOLD`, `WRITE_HOLD`;
  `header_fragments(rmap) -> dict[str, str]` (for `mb_rtu_app_temp.h`),
  `source_fragments(rmap) -> dict[str, str]` (for `mb_rtu_app_temp.c`).

- [ ] **Step 1: Write the failing tests**

Create `tests/test_c_modbus.py`:

```python
from helpers import resolved

from regmap.generators.c_modbus import (
    HOLD_DEFINE,
    INPUT_DEFINE,
    READ_HOLD,
    READ_INPUT,
    WRITE_HOLD,
    header_fragments,
    source_fragments,
)

MAP = """
SYS:
  code: 0
  registers:
    - {name: UPTIME, type: INT, access: RO, size: 4}
    - {name: MODE, type: ENUM, access: RWF, size: 1, values: [{name: M0}]}
    - {name: TEMP, type: FLOAT, access: RW, size: 4}
    - {name: EXACT, type: FLOAT, access: RW, size: 4, modbus: {format: F32}}
    - {name: HIDDEN, type: INT, access: RW, size: 2, modbus: false}
    - {name: LEVEL, type: INT, access: RW, size: 2, modbus: {address: 100}}
"""


def test_defines():
    header = header_fragments(resolved(MAP))
    # width = len("SYS_HIDDEN") = 10 -> the define name is padded to 10 + 14 characters
    assert header[INPUT_DEFINE] == (
        "#define MB_INPUT_FIRST    0\n"
        "\n"
        "#define MB_INPUT_SYS_UPTIME_0   0u\n"
        "#define MB_INPUT_SYS_UPTIME_1   1u\n"
        "\n"
        "#define MB_INPUT_LAST     1\n"
    )
    assert header[HOLD_DEFINE].splitlines() == [
        "#define MB_HOLD_FIRST     0",
        "",
        "#define MB_HOLD_SYS_MODE        0u",
        "#define MB_HOLD_SYS_TEMP        1u",
        "#define MB_HOLD_SYS_EXACT_0     2u",
        "#define MB_HOLD_SYS_EXACT_1     3u",
        "#define MB_HOLD_SYS_LEVEL       100u",
        "",
        "#define MB_HOLD_LAST      100",
    ]


def test_empty_space_defines():
    header = header_fragments(
        resolved("SYS: {code: 0, registers: [{name: A, type: INT, access: RO, size: 2}]}")
    )
    assert header[HOLD_DEFINE] == "#define MB_HOLD_FIRST     0\n\n\n#define MB_HOLD_LAST      -1\n"


def test_reads():
    source = source_fragments(resolved(MAP))
    assert source[READ_INPUT] == (
        "    case MB_INPUT_SYS_UPTIME_0:\n"
        "      *value = *((uint16_t *)CONF_PTR(CONF_SYS_UPTIME) + 0);\n"
        "      break;\n"
        "    case MB_INPUT_SYS_UPTIME_1:\n"
        "      *value = *((uint16_t *)CONF_PTR(CONF_SYS_UPTIME) + 1);\n"
        "      break;\n"
    )
    hold = source[READ_HOLD].splitlines()
    assert hold[:6] == [
        "    case MB_HOLD_SYS_MODE:",
        "      *value = conf.sys.mode;",
        "      break;",
        "    case MB_HOLD_SYS_TEMP:",
        "      *value = (int16_t)(10 * conf.sys.temp);",
        "      break;",
    ]
    assert "      *value = *((uint16_t *)CONF_PTR(CONF_SYS_EXACT) + 1);" in hold
    assert "HIDDEN" not in source[READ_HOLD]


def test_writes():
    assert source_fragments(resolved(MAP))[WRITE_HOLD] == (
        "    case MB_HOLD_SYS_MODE:\n"
        "      conf.sys.mode = (sys_mode_t)value;\n"
        "      id = CONF_SYS_MODE;\n"
        "      break;\n"
        "    case MB_HOLD_SYS_TEMP:\n"
        "      conf.sys.temp = ((float)((int16_t)value)) / 10;\n"
        "      id = CONF_SYS_TEMP;\n"
        "      break;\n"
        "    case MB_HOLD_SYS_EXACT_0:\n"
        "      *((uint16_t *)CONF_PTR(CONF_SYS_EXACT) + 0) = value;\n"
        "      break;\n"
        "    case MB_HOLD_SYS_EXACT_1:\n"
        "      *((uint16_t *)CONF_PTR(CONF_SYS_EXACT) + 1) = value;\n"
        "      id = CONF_SYS_EXACT;\n"
        "      break;\n"
        "    case MB_HOLD_SYS_LEVEL:\n"
        "      conf.sys.level = value;\n"
        "      id = CONF_SYS_LEVEL;\n"
        "      break;\n"
    )


def test_long_register_keeps_define_columns_apart():
    rmap = resolved("SYS: {code: 0, registers: [{name: TEXT, type: STRING, access: RO, size: 32}]}")
    lines = header_fragments(rmap)[INPUT_DEFINE].splitlines()
    assert len(lines) == 2 + 16 + 2
    assert lines[2] == "#define MB_INPUT_SYS_TEXT_0   0u"
    assert lines[17] == "#define MB_INPUT_SYS_TEXT_15  15u"


def test_huge_register_still_has_a_space_before_the_address():
    rmap = resolved(
        "SYS: {code: 0, registers: [{name: TEXT, type: STRING, access: RO, size: 2048}]}"
    )
    lines = header_fragments(rmap)[INPUT_DEFINE].splitlines()
    assert lines[-3] == "#define MB_INPUT_SYS_TEXT_1023 1023u"
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `pytest -q tests/test_c_modbus.py`
Expected: FAIL — `ModuleNotFoundError: No module named 'regmap.generators.c_modbus'`

- [ ] **Step 3: Implement**

Create `src/regmap/generators/c_modbus.py` with:

```python
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
```

- [ ] **Step 4: Run the whole suite and the linters**

Run: `pytest -q && ruff check . && ruff format --check .`
Expected: `102 passed` (6 of them in this task's tests), `All checks passed!`, all files already formatted.

- [ ] **Step 5: Documentation**

Append to `doc/outputs.md`:

```markdown
## `mb_rtu_app.h`

`MB_INPUT_FIRST` / `MB_HOLD_FIRST`, one `#define MB_<INPUT|HOLD>_<full name>[_<n>] <addr>u` per
Modbus word (suffix `_0`, `_1`, … for registers with more than one word), and
`MB_INPUT_LAST` / `MB_HOLD_LAST`.

## `mb_rtu_app.c`

`case` entries for the three callbacks:

- read input / read holding: `*value = conf.<block>.<member>;` for one-word registers,
  `*value = *((uint16_t *)CONF_PTR(CONF_X) + j);` per word of longer registers and
  `*value = (int16_t)(10 * conf.<block>.<member>);` for FLOAT ×10;
- write holding: the matching assignment (`(<enum>_t)value` for enums,
  `((float)((int16_t)value)) / 10` for FLOAT ×10) and `id = CONF_X;` on the last word, so the
  firmware applies the register once it is complete.
```

- [ ] **Step 6: Commit**

```bash
git add src/regmap/generators/c_modbus.py doc/outputs.md tests/test_c_modbus.py
git commit -m "feat: mb_rtu_app.h/.c fragments"
```

### Task 7: LeBin JSON, Modbus JSON and Python outputs

**Files:**
- Create: `src/regmap/generators/lebin_json.py`
- Create: `src/regmap/generators/modbus_json.py`
- Create: `src/regmap/generators/python_regs.py`
- Modify: `doc/outputs.md`
- Test: `tests/test_json_outputs.py`
- Test: `tests/test_python_regs.py`

**Interfaces:**
- Consumes: `regmap.generators.describe`, `regmap.resolve`, `regmap.codes`.
- Produces: `regmap.generators.lebin_json.render(rmap) -> str`,
  `regmap.generators.modbus_json.render(rmap) -> str`,
  `regmap.generators.python_regs.render(rmap) -> str` (all with `\n` line endings and a
  final newline).

- [ ] **Step 1: Write the failing tests**

Create `tests/test_json_outputs.py`:

```python
import json

from helpers import resolved

from regmap.generators import lebin_json, modbus_json

MAP = """
SYS:
  code: 0
  registers:
    - name: VERSION
      type: INT
      access: ROF
      size: 4
      label: Version
      default: 1001
      min: 1001
      max: 0x3FF0
      unit: "-"
      description: |-
        Line one
        Line two
    - {name: STATUS, type: BIN, access: RO, size: 4, label: Status, description: Flags, bits: [{name: S_ERR, bit: 0, label: Error, description: Any error}, {name: S_WD, bit: 17, label: Watchdog}]}
    - {name: RAW, type: BIN, access: RO, size: 2}
    - {name: MODE, type: ENUM, access: RWIF, size: 1, default: M_B, values: [{name: M_A, label: A, description: First}, {name: M_B, value: 4}]}
    - {name: TEMP, type: FLOAT, access: RW, size: 4, default: 21.5, modbus: {format: F32}}
    - {name: TEXT, type: STRING, access: RW, size: 8, default: hi}
    - {name: HIDDEN, type: INT, access: RW, size: 2, modbus: false}
"""


def lebin() -> dict[str, dict]:
    return {e["Name"]: e for e in json.loads(lebin_json.render(resolved(MAP)))}


def modbus() -> dict[str, dict]:
    return {e["Name"]: e for e in json.loads(modbus_json.render(resolved(MAP)))}


def test_lebin_plain_register():
    assert lebin()["VERSION"] == {
        "Category": "SYS",
        "Name": "VERSION",
        "Label": "Version",
        "Id": 0x00000132,
        "VarType": "INT",
        "Access": "ROF",
        "Description": "Line one\r\nLine two",
        "Value": "1001",
        "NewValue": "1001",
        "Reset": "1001",
        "Min": 1001,
        "Max": 0x3FF0,
        "Units": "-",
    }
    assert list(lebin()["VERSION"]) == [
        "Category", "Name", "Label", "Id", "VarType", "Access", "Description",
        "Value", "NewValue", "Reset", "Min", "Max", "Units",
    ]  # fmt: skip


def test_lebin_bits_enum_and_access():
    entries = lebin()
    status = entries["STATUS"]
    assert status["Description"] == (
        "Flags\r\nMeaning of respective bits: \r\n[0] - Error - Any error.\r\n[17] - Watchdog.\r\n"
    )
    assert (status["EnumStr"], status["EnumValue"], status["Value"]) == (
        ["Error", "Watchdog"],
        [0, 17],
        "",
    )
    raw = entries["RAW"]
    assert (raw["EnumStr"], raw["EnumValue"], raw["Description"]) == ([], [], "")
    mode = entries["MODE"]
    assert mode["Access"] == "RWF"
    assert (mode["Value"], mode["EnumStr"], mode["EnumValue"]) == ("4", ["A", "M_B"], [0, 4])
    assert mode["Description"] == "Allowed values: \r\nA - First.\r\nM_B.\r\n"
    assert "Min" not in mode and "Max" not in mode
    assert list(mode)[8:12] == ["NewValue", "EnumStr", "EnumValue", "Reset"]
    assert entries["TEMP"]["Value"] == "21.5"
    assert entries["TEXT"]["Value"] == "hi"
    assert "IsVisible" not in mode and "ConfigUser" not in mode
    assert len(entries) == 7


def test_modbus_entries():
    entries = modbus()
    assert "SYS_HIDDEN" not in entries
    assert entries["SYS_VERSION"] == {
        "Type": "INPUT",
        "Name": "SYS_VERSION",
        "Id": 0x00000132,
        "Address": [0, 1],
        "Format": "INT",
        "Value": 1001,
        "Access": "ROF",
        "Min": 1001,
        "Max": 0x3FF0,
        "Unit": "-",
        "Label": "Version",
        "Description": "Line one\r\nLine two",
    }
    status = entries["SYS_STATUS"]
    assert (status["Min"], status["Max"], status["Value"]) == (0, 0, 0)
    assert status["Description"].endswith("Bit 0 - Error - Any error.\r\nBit 17 - Watchdog.\r\n")
    mode = entries["SYS_MODE"]
    assert (mode["Type"], mode["Access"], mode["Min"], mode["Max"], mode["Value"]) == (
        "HOLD",
        "RWIF",
        0,
        4,
        4,
    )
    assert mode["Description"] == "Allowed values: \r\nValue 0 - A - First.\r\nValue 4 - M_B.\r\n"
    assert list(mode)[-3:] == ["EnumStr", "EnumValue", "Description"]
    assert (entries["SYS_TEMP"]["Format"], entries["SYS_TEMP"]["Address"]) == ("FLOAT32", [1, 2])
    assert entries["SYS_TEXT"]["Value"] == "hi"


def test_json_text_layout():
    text = lebin_json.render(resolved(MAP))
    assert text.startswith('[\n  {\n    "Category": "SYS",\n')
    assert text.endswith("}\n]\n")
```

Create `tests/test_python_regs.py`:

```python
from helpers import resolved

from regmap.generators import python_regs


def test_python_class_with_enums():
    rmap = resolved(
        """
        SYS:
          code: 0
          registers:
            - {name: UPTIME, type: INT, access: RO, size: 4}
            - {name: MODE, type: ENUM, access: RW, size: 1, values: [{name: M_A}, {name: M_B, value: 3}]}
        """
    )
    assert python_regs.render(rmap) == (
        "class DevRegs:\n"
        '    SYS_UPTIME = "SYS_UPTIME"\n'
        '    SYS_MODE = "SYS_MODE"\n'
        "\n\n"
        "class SYS_MODE:\n"
        "    M_A = 0\n"
        "    M_B = 3\n"
        "\n\n"
    )


def test_map_without_registers_is_still_valid_python():
    text = python_regs.render(resolved("SYS: {code: 0, registers: []}"))
    assert text == "class DevRegs:\n    pass\n\n\n"
    compile(text, "DevRegs.py", "exec")
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `pytest -q tests/test_json_outputs.py tests/test_python_regs.py`
Expected: FAIL — `ImportError: cannot import name 'lebin_json' from 'regmap.generators'`

- [ ] **Step 3: Implement**

Create `src/regmap/generators/lebin_json.py` with:

```python
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
```

Create `src/regmap/generators/modbus_json.py` with:

```python
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
```

Create `src/regmap/generators/python_regs.py` with:

```python
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
```

- [ ] **Step 4: Run the whole suite and the linters**

Run: `pytest -q && ruff check . && ruff format --check .`
Expected: `108 passed` (6 of them in this task's tests), `All checks passed!`, all files already formatted.

- [ ] **Step 5: Documentation**

Append to `doc/outputs.md`:

````markdown
## LeBin JSON — `<name>_registers.json`

A list with one object per register:

| key | value |
|---|---|
| `Category` | block abbreviation |
| `Name` | register name without the block |
| `Label` | label or `""` |
| `Id` | register ID (decimal) |
| `VarType` | type |
| `Access` | access; `RWIF` is written as `RWF` |
| `Description` | description + legend |
| `Value`, `NewValue` | default as a string (`""` when none, enums as their number) |
| `EnumStr`, `EnumValue` | ENUM and BIN only: labels and values / bit numbers (empty lists for BIN without bits) |
| `Reset` | same as `Value` |
| `Min`, `Max` | only when set in the YAML |
| `Units` | unit or `""` |

## Modbus JSON — `<name>_Modbus.json`

A list with one object per register exposed on Modbus:

| key | value |
|---|---|
| `Type` | `INPUT` or `HOLD` |
| `Name` | full name |
| `Id` | register ID (decimal) |
| `Address` | list of Modbus addresses |
| `Format` | type; `FLOAT32` for `format: F32` |
| `Value` | default as a number (0 when none; enums as their number; strings as text) |
| `Access` | access as written |
| `Min`, `Max` | `min`/`max` or 0; derived for ENUM |
| `Unit`, `Label` | text or `""` |
| `EnumStr`, `EnumValue` | ENUM and BIN only |
| `Description` | description + legend |

## Legend in descriptions

ENUM registers and BIN registers with bits get a legend appended to `Description`: an empty
line separator when the register has a description, then `Allowed values: ` (ENUM) or
`Meaning of respective bits: ` (BIN) and one line per item, `<label> - <description>.` or
`<label>.` when the item has no description. The item prefix is `""` / `[<bit>] - ` in LeBin
JSON and `Value <n> - ` / `Bit <n> - ` in Modbus JSON. Line breaks are written as `\r\n`.

## `<name>Regs.py`

A class `<name>Regs` with one string constant per register full name, then one class per ENUM
register with its values:

```python
class Vms1511Regs:
    SYS_UPTIME = "SYS_UPTIME"


class COM_MB_BAUD_RATE:
    MB_BAUD_9600 = 0
```
````

- [ ] **Step 6: Commit**

```bash
git add src/regmap/generators/lebin_json.py src/regmap/generators/modbus_json.py src/regmap/generators/python_regs.py doc/outputs.md tests/test_json_outputs.py tests/test_python_regs.py
git commit -m "feat: LeBin JSON, Modbus JSON and Python outputs"
```

### Task 8: Output rendering and file writing

**Files:**
- Create: `src/regmap/outputs.py`
- Create: `src/regmap/writer.py`
- Test: `tests/test_outputs.py`

**Interfaces:**
- Consumes: all generators (Tasks 5–7), `regmap.templating` (`fill`, `load_template`),
  `regmap.model.GeneratorSettings`.
- Produces:
  - `regmap.outputs`: `OUTPUT_KEYS`, `@dataclass(frozen=True) OutputFile(key: str, path: Path,
    text: str)`, `destination(key, settings, base_dir, out_dir) -> Path`,
    `render_outputs(rmap, settings, base_dir: Path, out_dir: Path | None = None) ->
    list[OutputFile]` (order: reg_map.h, reg_map.c, mb_rtu_app.h, mb_rtu_app.c, LeBin JSON,
    Modbus JSON, Python).
  - `regmap.writer`: `encode(text) -> bytes` (CRLF, UTF-8), `is_current(output) -> bool`,
    `stale(outputs) -> list[Path]`, `write(outputs) -> list[tuple[Path, str]]` with status
    `"written"` / `"unchanged"`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_outputs.py`:

```python
import os
from pathlib import Path

from helpers import load

from regmap.outputs import render_outputs
from regmap.resolve import resolve
from regmap.writer import encode, stale, write

BLOCKS = "SYS: {code: 0, registers: [{name: A, type: INT, access: RO, size: 2, label: A}]}"
DESTINATIONS = """\
generator:
  templates: tpl
  outputs:
    reg_map: fw/common
    modbus: fw/serial
    lebin_json: tests
"""


def outputs(base: Path, out: Path | None = None, extra: str = ""):
    rmap = load(BLOCKS, extra=extra)
    return render_outputs(resolve(rmap), rmap.generator, base, out)


def test_file_names_and_default_destination(tmp_path):
    files = outputs(tmp_path)
    assert [(o.key, o.path.relative_to(tmp_path).as_posix()) for o in files] == [
        ("reg_map", "reg_map.h"),
        ("reg_map", "reg_map.c"),
        ("modbus", "mb_rtu_app.h"),
        ("modbus", "mb_rtu_app.c"),
        ("lebin_json", "Dev_registers.json"),
        ("modbus_json", "Dev_Modbus.json"),
        ("python", "DevRegs.py"),
    ]
    assert "#define CONF_SYS_A " in files[0].text


def test_configured_destinations_and_project_templates(tmp_path):
    (tmp_path / "tpl").mkdir()
    (tmp_path / "tpl" / "reg_map_temp.c").write_text(
        "// project\n/* < DEFINE REG MAP STORAGE > */\n/* < REG MAP FACTORY > */\n"
    )
    files = {
        o.path.relative_to(tmp_path).as_posix(): o for o in outputs(tmp_path, extra=DESTINATIONS)
    }
    assert sorted(files) == [
        "DevRegs.py",
        "Dev_Modbus.json",
        "fw/common/reg_map.c",
        "fw/common/reg_map.h",
        "fw/serial/mb_rtu_app.c",
        "fw/serial/mb_rtu_app.h",
        "tests/Dev_registers.json",
    ]
    assert files["fw/common/reg_map.c"].text.startswith("// project\nconf_reg_t conf;\n")


def test_out_dir_overrides_configured_destinations(tmp_path):
    files = outputs(
        tmp_path, out=tmp_path / "gen", extra=DESTINATIONS.replace("  templates: tpl\n", "")
    )
    assert {o.path.parent for o in files} == {tmp_path / "gen"}


def test_writer_uses_crlf_and_skips_unchanged_files(tmp_path):
    files = outputs(tmp_path)
    assert encode("a\nb\n") == b"a\r\nb\r\n"
    assert stale(files) == [o.path for o in files]
    assert {status for _, status in write(files)} == {"written"}
    assert b"\r\n" in files[0].path.read_bytes() and b"\r\r" not in files[0].path.read_bytes()
    os.utime(files[0].path, (1_000_000, 1_000_000))
    assert {status for _, status in write(files)} == {"unchanged"}
    assert files[0].path.stat().st_mtime == 1_000_000
    assert stale(files) == []


def test_writer_creates_missing_destination_directories(tmp_path):
    files = outputs(tmp_path, extra="generator: {outputs: {reg_map: deep/new/dir}}\n")
    write(files)
    assert (tmp_path / "deep" / "new" / "dir" / "reg_map.h").is_file()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `pytest -q tests/test_outputs.py`
Expected: FAIL — `ModuleNotFoundError: No module named 'regmap.outputs'`

- [ ] **Step 3: Implement**

Create `src/regmap/outputs.py` with:

```python
"""Render all output files of a map in memory and decide where they go."""

from dataclasses import dataclass
from pathlib import Path

from regmap.generators import c_modbus, c_regmap, lebin_json, modbus_json, python_regs
from regmap.model import GeneratorSettings
from regmap.resolve import ResolvedMap
from regmap.templating import fill, load_template

OUTPUT_KEYS = ("reg_map", "modbus", "lebin_json", "modbus_json", "python")


@dataclass(frozen=True)
class OutputFile:
    key: str  # one of OUTPUT_KEYS
    path: Path
    text: str  # "\n" line endings; the writer converts to CRLF


def destination(
    key: str, settings: GeneratorSettings, base_dir: Path, out_dir: Path | None
) -> Path:
    if out_dir is not None:
        return out_dir
    configured = getattr(settings.outputs, key)
    return base_dir / configured if configured else base_dir


def render_outputs(
    rmap: ResolvedMap, settings: GeneratorSettings, base_dir: Path, out_dir: Path | None = None
) -> list[OutputFile]:
    """All output files; ``base_dir`` is the YAML file's directory."""
    project = base_dir / settings.templates if settings.templates else None

    def c_file(key: str, template: str, output: str, fragments: dict[str, str]) -> OutputFile:
        text = fill(load_template(template, project), fragments, template)
        return OutputFile(key, destination(key, settings, base_dir, out_dir) / output, text)

    def data_file(key: str, filename: str, text: str) -> OutputFile:
        return OutputFile(key, destination(key, settings, base_dir, out_dir) / filename, text)

    return [
        c_file("reg_map", "reg_map_temp.h", "reg_map.h", c_regmap.header_fragments(rmap)),
        c_file("reg_map", "reg_map_temp.c", "reg_map.c", c_regmap.source_fragments(rmap)),
        c_file("modbus", "mb_rtu_app_temp.h", "mb_rtu_app.h", c_modbus.header_fragments(rmap)),
        c_file("modbus", "mb_rtu_app_temp.c", "mb_rtu_app.c", c_modbus.source_fragments(rmap)),
        data_file("lebin_json", f"{rmap.name}_registers.json", lebin_json.render(rmap)),
        data_file("modbus_json", f"{rmap.name}_Modbus.json", modbus_json.render(rmap)),
        data_file("python", f"{rmap.name}Regs.py", python_regs.render(rmap)),
    ]
```

Create `src/regmap/writer.py` with:

```python
"""Write output files: UTF-8 without BOM, CRLF line endings, unchanged files left alone."""

from pathlib import Path

from regmap.outputs import OutputFile


def encode(text: str) -> bytes:
    return text.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8")


def is_current(output: OutputFile) -> bool:
    return output.path.is_file() and output.path.read_bytes() == encode(output.text)


def stale(outputs: list[OutputFile]) -> list[Path]:
    """Files that are missing or differ from what would be written (for --check)."""
    return [o.path for o in outputs if not is_current(o)]


def write(outputs: list[OutputFile]) -> list[tuple[Path, str]]:
    """Write changed files; returns (path, "written" | "unchanged") per file."""
    report = []
    for o in outputs:
        if is_current(o):
            report.append((o.path, "unchanged"))
            continue
        o.path.parent.mkdir(parents=True, exist_ok=True)
        o.path.write_bytes(encode(o.text))
        report.append((o.path, "written"))
    return report
```

- [ ] **Step 4: Run the whole suite and the linters**

Run: `pytest -q && ruff check . && ruff format --check .`
Expected: `113 passed` (5 of them in this task's tests), `All checks passed!`, all files already formatted.

- [ ] **Step 5: Commit**

```bash
git add src/regmap/outputs.py src/regmap/writer.py tests/test_outputs.py
git commit -m "feat: render outputs in memory and write changed files"
```

### Task 9: Stability check against previous outputs

**Files:**
- Create: `src/regmap/check.py`
- Create: `doc/stability-check.md`
- Test: `tests/test_check.py`

**Interfaces:**
- Consumes: `regmap.outputs.OutputFile`, `regmap.templating.read_text_file`.
- Produces (`regmap.check`): `@dataclass(frozen=True) Change(register, what, old, new)`,
  `class PreviousOutputError(Exception)`, `compare_lebin(old, new) -> list[Change]`,
  `compare_modbus(old, new) -> list[Change]`, `breaking_changes(outputs) -> list[Change]`,
  `format_changes(changes) -> str`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_check.py`:

```python
import json

import pytest
from helpers import load

from regmap.check import (
    Change,
    PreviousOutputError,
    breaking_changes,
    compare_lebin,
    compare_modbus,
    format_changes,
)
from regmap.outputs import render_outputs
from regmap.resolve import resolve

BASE = """
SYS:
  code: 0
  registers:
    - {name: A, type: INT, access: RW, size: 2}
    - {name: B, type: INT, access: RW, size: 2}
"""


def render(blocks: str, base):
    rmap = load(blocks)
    return render_outputs(resolve(rmap), rmap.generator, base)


def write_previous(files) -> None:
    for o in files:
        o.path.write_text(o.text, encoding="utf-8")


def test_first_generation_has_nothing_to_compare(tmp_path):
    assert breaking_changes(render(BASE, tmp_path)) == []


def test_same_map_has_no_changes(tmp_path):
    write_previous(render(BASE, tmp_path))
    assert breaking_changes(render(BASE, tmp_path)) == []


def test_inserted_register_shifts_id_and_modbus_address(tmp_path):
    write_previous(render(BASE, tmp_path))
    inserted = BASE.replace(
        "    - {name: B", "    - {name: NEW, type: INT, access: RW, size: 2}\n    - {name: B"
    )
    assert breaking_changes(render(inserted, tmp_path)) == [
        Change("SYS_B", "Id", "0x00002151", "0x00004151"),
        Change("SYS_B", "Modbus", "HOLD [1]", "HOLD [2]"),
    ]


def test_removed_register_and_removed_from_modbus(tmp_path):
    write_previous(render(BASE, tmp_path))
    changed = BASE.replace("    - {name: B, type: INT, access: RW, size: 2}\n", "").replace(
        "size: 2}", "size: 2, modbus: false}"
    )
    assert breaking_changes(render(changed, tmp_path)) == [
        Change("SYS_B", "register", "present", "removed"),
        Change("SYS_A", "Modbus", "HOLD [0]", "not exposed"),
        Change("SYS_B", "Modbus", "HOLD [1]", "not exposed"),
    ]


def test_legacy_cp1250_lebin_file_is_compared(tmp_path):
    files = render(BASE, tmp_path)
    legacy = [{"Category": "SYS", "Name": "A", "Id": 1, "Description": "Hodnota …"}]
    files[4].path.write_bytes(json.dumps(legacy, ensure_ascii=False).encode("cp1250"))
    assert breaking_changes(files) == [Change("SYS_A", "Id", "0x00000001", "0x00000151")]


def test_unreadable_previous_file_is_reported(tmp_path):
    files = render(BASE, tmp_path)
    files[5].path.write_text("{ not json", encoding="utf-8")
    with pytest.raises(PreviousOutputError, match="cannot read previous file"):
        breaking_changes(files)


def test_entries_without_names_are_ignored():
    assert compare_lebin([{"Id": 5}], []) == []
    assert compare_modbus([{"Type": "HOLD"}], []) == []


def test_format_changes_aligns_register_names():
    text = format_changes(
        [Change("SYS_A", "Id", "0x1", "0x2"), Change("SYS_LONG", "Modbus", "HOLD [1]", "HOLD [2]")]
    )
    assert text == "  SYS_A     Id: 0x1 -> 0x2\n  SYS_LONG  Modbus: HOLD [1] -> HOLD [2]"
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `pytest -q tests/test_check.py`
Expected: FAIL — `ModuleNotFoundError: No module named 'regmap.check'`

- [ ] **Step 3: Implement**

Create `src/regmap/check.py` with:

```python
"""Stability check: compare IDs and Modbus addresses with the previously generated JSON files."""

import json
from dataclasses import dataclass
from typing import Any

from regmap.outputs import OutputFile
from regmap.templating import read_text_file


@dataclass(frozen=True)
class Change:
    register: str
    what: str
    old: str
    new: str


class PreviousOutputError(Exception):
    """A previous output file exists but cannot be read as a JSON list."""


def _previous(output: OutputFile) -> list[dict[str, Any]] | None:
    if not output.path.is_file():
        return None
    try:
        data = json.loads(read_text_file(output.path))
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        raise PreviousOutputError(f"{output.path}: cannot read previous file: {exc}") from None
    if not isinstance(data, list):
        raise PreviousOutputError(f"{output.path}: previous file is not a JSON list")
    return [e for e in data if isinstance(e, dict)]


def _hex(value: Any) -> str:
    return f"0x{value:08X}" if isinstance(value, int) else str(value)


def _placement(entry: dict[str, Any]) -> str:
    return f"{entry.get('Type')} {entry.get('Address')}"


def compare_lebin(old: list[dict[str, Any]], new: list[dict[str, Any]]) -> list[Change]:
    current = {(e.get("Category"), e.get("Name")): e for e in new}
    changes = []
    for e in old:
        key = (e.get("Category"), e.get("Name"))
        if None in key or "Id" not in e:
            continue
        name = f"{key[0]}_{key[1]}"
        if key not in current:
            changes.append(Change(name, "register", "present", "removed"))
        elif current[key].get("Id") != e["Id"]:
            changes.append(Change(name, "Id", _hex(e["Id"]), _hex(current[key].get("Id"))))
    return changes


def compare_modbus(old: list[dict[str, Any]], new: list[dict[str, Any]]) -> list[Change]:
    current = {e.get("Name"): e for e in new}
    changes = []
    for e in old:
        name = e.get("Name")
        if name is None:
            continue
        if name not in current:
            changes.append(Change(name, "Modbus", _placement(e), "not exposed"))
        elif _placement(current[name]) != _placement(e):
            changes.append(Change(name, "Modbus", _placement(e), _placement(current[name])))
    return changes


def breaking_changes(outputs: list[OutputFile]) -> list[Change]:
    """Breaking changes against the files currently on disk (empty on the first generation)."""
    changes = []
    for output in outputs:
        compare = {"lebin_json": compare_lebin, "modbus_json": compare_modbus}.get(output.key)
        if compare is None:
            continue
        old = _previous(output)
        if old is not None:
            changes += compare(old, json.loads(output.text))
    return changes


def format_changes(changes: list[Change]) -> str:
    width = max(len(c.register) for c in changes)
    lines = [f"  {c.register.ljust(width)}  {c.what}: {c.old} -> {c.new}" for c in changes]
    return "\n".join(lines)
```

- [ ] **Step 4: Run the whole suite and the linters**

Run: `pytest -q && ruff check . && ruff format --check .`
Expected: `121 passed` (8 of them in this task's tests), `All checks passed!`, all files already formatted.

- [ ] **Step 5: Documentation**

Create `doc/stability-check.md`:

````markdown
# Stability check

Communication software addresses registers by ID (LeBin) and by Modbus address. Inserting a
register in the middle of a block silently shifts both for every following register, so
`regmap generate` compares the new outputs with the JSON files it generated last time before
writing anything.

## What is compared

The previous `<name>_registers.json` and `<name>_Modbus.json` are read from their destination
directories (UTF-8, or cp1250 for files produced by the old VBA macros).

| file | matched by | breaking change |
|---|---|---|
| LeBin JSON | `Category` + `Name` | different `Id`; register missing |
| Modbus JSON | `Name` | different `Type` or `Address`; register no longer exposed |

New registers and changes of labels, descriptions, units, defaults, ranges and enum/bit texts
are not breaking. When no previous file exists (first generation) the check is skipped.

## When something breaks

`regmap generate` prints the list and exits with code 1 without writing any file:

```
error: breaking changes against the previous outputs, nothing written:
  SYS_TEST      Id: 0x00010152 -> 0x00014152
  SYS_TEST      Modbus: HOLD [2, 3] -> HOLD [4, 5]
use --allow-id-change if the change is intended
```

Either fix the map (e.g. move the new register to the end of the block or give it a free
`address`) or, for an intended change such as a new major register map version, run again with
`--allow-id-change`; the list is then printed as a warning and the files are written.

A previous JSON file that cannot be read is also an error; `--allow-id-change` overwrites it.
````

- [ ] **Step 6: Commit**

```bash
git add src/regmap/check.py doc/stability-check.md tests/test_check.py
git commit -m "feat: detect changed register IDs and Modbus addresses"
```

### Task 10: Command line: generate and schema

**Files:**
- Create: `src/regmap/cli.py`
- Test: `tests/test_cli.py`

**Interfaces:**
- Consumes: `load_map`, `MapError`, `RegisterMap` (Task 2), `resolve` (Task 3),
  `TemplateError` (Task 4), `render_outputs` (Task 8), `encode`, `stale`, `write` (Task 8),
  `breaking_changes`, `format_changes`, `PreviousOutputError` (Task 9).
- Produces (`regmap.cli`): `main(argv: Sequence[str] | None = None) -> int` (console script
  `regmap`), `build_parser() -> argparse.ArgumentParser`; subcommands `generate` and `schema`
  (Task 12 adds `import-xlsx`).

- [ ] **Step 1: Write the failing tests**

Create `tests/test_cli.py`:

```python
import json

import pytest
from helpers import map_yaml

from regmap import __version__
from regmap.cli import main

BLOCKS = "SYS: {code: 0, registers: [{name: A, type: INT, access: RW, size: 2}]}"


@pytest.fixture
def map_file(tmp_path):
    path = tmp_path / "dev.yaml"
    path.write_text(map_yaml(BLOCKS), encoding="utf-8")
    return path


def test_generate_writes_then_reports_unchanged(map_file, capsys):
    assert main(["generate", str(map_file)]) == 0
    assert capsys.readouterr().out.count("written") == 7
    assert (map_file.parent / "reg_map.h").is_file()
    assert main(["generate", str(map_file)]) == 0
    assert capsys.readouterr().out.count("unchanged") == 7


def test_check_mode(map_file, capsys):
    assert main(["generate", str(map_file), "--check"]) == 1
    assert capsys.readouterr().out.count("outdated") == 7
    assert not (map_file.parent / "reg_map.h").exists()
    main(["generate", str(map_file)])
    assert main(["generate", str(map_file), "--check"]) == 0


def test_out_option(map_file, tmp_path):
    assert main(["generate", str(map_file), "--out", str(tmp_path / "gen")]) == 0
    assert (tmp_path / "gen" / "Dev_Modbus.json").is_file()


def test_breaking_change_needs_confirmation(map_file, capsys):
    main(["generate", str(map_file)])
    before = (map_file.parent / "reg_map.h").read_bytes()
    map_file.write_text(
        map_yaml(
            BLOCKS.replace("[{name: A", "[{name: NEW, type: INT, access: RW, size: 2}, {name: A")
        )
    )
    capsys.readouterr()
    assert main(["generate", str(map_file)]) == 1
    err = capsys.readouterr().err
    assert "breaking changes against the previous outputs, nothing written" in err
    assert "SYS_A  Id: 0x00000151 -> 0x00002151" in err
    assert (map_file.parent / "reg_map.h").read_bytes() == before
    assert main(["generate", str(map_file), "--allow-id-change"]) == 0
    assert "warning: breaking changes accepted" in capsys.readouterr().err
    assert (map_file.parent / "reg_map.h").read_bytes() != before


def test_corrupt_previous_output_needs_confirmation(map_file, capsys):
    (map_file.parent / "Dev_Modbus.json").write_text("garbage")
    assert main(["generate", str(map_file)]) == 1
    assert "use --allow-id-change to overwrite it" in capsys.readouterr().err
    assert main(["generate", str(map_file), "--allow-id-change"]) == 0


def test_invalid_map_reports_all_problems(tmp_path, capsys):
    path = tmp_path / "bad.yaml"
    path.write_text(
        map_yaml("SYS: {code: 0, registers: [{name: A, type: INT, access: RW, size: 3, x: 1}]}")
    )
    assert main(["generate", str(path)]) == 1
    err = capsys.readouterr().err
    assert f"error: {path}: 2 problem(s)" in err
    assert "blocks.SYS.registers[0] (A).size: size 3 is not allowed for INT" in err
    assert "blocks.SYS.registers[0] (A).x: unknown key" in err


def test_template_without_placeholder_fails(tmp_path, capsys):
    (tmp_path / "tpl").mkdir()
    (tmp_path / "tpl" / "reg_map_temp.h").write_text("nothing here\n")
    path = tmp_path / "dev.yaml"
    path.write_text(map_yaml(BLOCKS, extra="generator: {templates: tpl}\n"))
    assert main(["generate", str(path)]) == 1
    assert (
        "reg_map_temp.h: placeholder /* < DEFINE REG MAP > */ not found" in capsys.readouterr().err
    )


def test_missing_file_and_bad_arguments_exit_2(tmp_path, capsys):
    assert main(["generate", str(tmp_path / "missing.yaml")]) == 2
    with pytest.raises(SystemExit) as exc:
        main(["generate"])
    assert exc.value.code == 2


def test_schema(tmp_path, capsys):
    assert main(["schema"]) == 0
    schema = json.loads(capsys.readouterr().out)
    assert set(schema["properties"]) == {"device", "generator", "blocks"}
    assert main(["schema", "-o", str(tmp_path / "s.json")]) == 0
    assert json.loads((tmp_path / "s.json").read_text()) == schema


def test_version(capsys):
    with pytest.raises(SystemExit):
        main(["--version"])
    assert capsys.readouterr().out.strip() == f"regmap {__version__}"


def test_relative_map_path_writes_next_to_the_map(tmp_path, monkeypatch):
    project = tmp_path / "project"
    project.mkdir()
    (project / "dev.yaml").write_text(
        map_yaml(BLOCKS, extra="generator: {outputs: {python: py}}\n")
    )
    elsewhere = tmp_path / "elsewhere"
    elsewhere.mkdir()
    monkeypatch.chdir(elsewhere)
    assert main(["generate", "../project/dev.yaml"]) == 0
    assert (project / "reg_map.h").is_file()
    assert (project / "py" / "DevRegs.py").is_file()
    assert list(elsewhere.iterdir()) == []
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `pytest -q tests/test_cli.py`
Expected: FAIL — `ModuleNotFoundError: No module named 'regmap.cli'`

- [ ] **Step 3: Implement**

Create `src/regmap/cli.py` with:

```python
"""Command line interface: regmap generate | schema."""

import argparse
import json
import sys
from collections.abc import Sequence
from pathlib import Path

from regmap import __version__
from regmap.check import PreviousOutputError, breaking_changes, format_changes
from regmap.model import MapError, RegisterMap, load_map
from regmap.outputs import render_outputs
from regmap.resolve import resolve
from regmap.templating import TemplateError
from regmap.writer import encode, stale, write


def _error(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)


def _generate(args: argparse.Namespace) -> int:
    path: Path = args.map
    if not path.is_file():
        _error(f"{path}: file not found")
        return 2
    try:
        rmap = load_map(path)
        resolved = resolve(rmap)
        outputs = render_outputs(resolved, rmap.generator, path.parent, args.out)
    except MapError as exc:
        _error(f"{path}: {len(exc.errors)} problem(s)")
        for message in exc.errors:
            print(f"  {message}", file=sys.stderr)
        return 1
    except TemplateError as exc:
        _error(str(exc))
        return 1

    if args.check:
        outdated = stale(outputs)
        for p in outdated:
            print(f"outdated  {p}")
        return 1 if outdated else 0

    try:
        changes = breaking_changes(outputs)
    except PreviousOutputError as exc:
        if not args.allow_id_change:
            _error(f"{exc}; use --allow-id-change to overwrite it")
            return 1
        changes = []
    if changes:
        table = format_changes(changes)
        if not args.allow_id_change:
            _error(f"breaking changes against the previous outputs, nothing written:\n{table}")
            print("use --allow-id-change if the change is intended", file=sys.stderr)
            return 1
        print(f"warning: breaking changes accepted (--allow-id-change):\n{table}", file=sys.stderr)

    for p, status in write(outputs):
        print(f"{status:9} {p}")
    return 0


def _schema(args: argparse.Namespace) -> int:
    text = json.dumps(RegisterMap.model_json_schema(), indent=2) + "\n"
    if args.output is None:
        sys.stdout.write(text)
    else:
        args.output.write_bytes(encode(text))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="regmap", description="Register map generator")
    parser.add_argument("--version", action="version", version=f"regmap {__version__}")
    sub = parser.add_subparsers(dest="command", required=True)

    gen = sub.add_parser("generate", help="generate C and JSON outputs from a YAML map")
    gen.add_argument("map", type=Path, help="register map YAML file")
    gen.add_argument("--out", type=Path, help="write all outputs to this directory")
    gen.add_argument("--allow-id-change", action="store_true", help="accept breaking changes")
    gen.add_argument("--check", action="store_true", help="only check that outputs are current")
    gen.set_defaults(func=_generate)

    sch = sub.add_parser("schema", help="print the JSON Schema of the YAML format")
    sch.add_argument("-o", "--output", type=Path, help="write the schema to this file")
    sch.set_defaults(func=_schema)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)
```

- [ ] **Step 4: Run the whole suite and the linters**

Run: `pytest -q && ruff check . && ruff format --check .`
Expected: `132 passed` (11 of them in this task's tests), `All checks passed!`, all files already formatted.

- [ ] **Step 5: Commit**

```bash
git add src/regmap/cli.py tests/test_cli.py
git commit -m "feat: regmap generate and schema commands"
```

### Task 11: Readable YAML writer

**Files:**
- Create: `src/regmap/importers/__init__.py`
- Create: `src/regmap/importers/yaml_emit.py`
- Test: `tests/test_yaml_emit.py`

**Interfaces:**
- Consumes: `regmap.model.load_map_text` (tests only).
- Produces (`regmap.importers.yaml_emit`): `HEADER`, `REGISTER_KEYS`,
  `class HexInt(int)` (constructed from text such as `"0x3FF"`, attribute `text`),
  `scalar(value, flow=False) -> str`, `emit_map(doc: dict) -> str` where `doc` is
  `{"device": {...}, "generator": {...} (optional), "blocks": {abbrev: {"code", "name"?,
  "description"?, "registers": [register dicts with keys of REGISTER_KEYS plus "values" /
  "bits" lists]}}}`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_yaml_emit.py`:

```python
import pytest
import yaml

from regmap.importers.yaml_emit import HexInt, emit_map, scalar
from regmap.model import load_map_text


@pytest.mark.parametrize(
    ("value", "flow", "expected"),
    [
        ("NONE", False, "NONE"),
        ("9600", False, '"9600"'),
        ("ON", False, '"ON"'),
        ("a: b", False, '"a: b"'),
        ("a, b", False, "a, b"),
        ("a, b", True, '"a, b"'),
        ("x #y", False, '"x #y"'),
        ("", False, '""'),
        (" padded", False, '" padded"'),
        ("Žluťoučký", False, "Žluťoučký"),
        (1.5, False, "1.5"),
        (1e-07, False, "1.0e-07"),
        (-3, False, "-3"),
        (False, False, "false"),
    ],
)
def test_scalar(value, flow, expected):
    assert scalar(value, flow) == expected
    text = f"{{k: {expected}}}" if flow else f"k: {expected}"
    assert yaml.safe_load(text) == {"k": value}


def test_hex_int_keeps_its_notation():
    value = HexInt("0x3FF")
    assert value == 1023
    assert scalar(value) == "0x3FF"


DOC = {
    "device": {"name": "Dev"},
    "generator": {"outputs": {"reg_map": "../fw"}},
    "blocks": {
        "SYS": {
            "code": 0,
            "name": "SYSTEM",
            "registers": [
                {
                    "name": "STATUS",
                    "type": "BIN",
                    "access": "RO",
                    "size": 4,
                    "description": "Line one\nLine two",
                    "bits": [
                        {"name": "S_ERR", "bit": 0, "label": "Error", "description": "Any, error"},
                        {"name": "S_LONGER_NAME", "bit": 17},
                    ],
                },
                {
                    "name": "MODE",
                    "type": "ENUM",
                    "access": "RWF",
                    "size": 1,
                    "default": "M_B",
                    "modbus": {"address": 100},
                    "values": [{"name": "M_A", "label": "9600"}, {"name": "M_B", "value": 5}],
                },
                {
                    "name": "MAX",
                    "type": "INT",
                    "access": "RW",
                    "size": 2,
                    "max": HexInt("0x3FF"),
                    "modbus": False,
                },
            ],
        }
    },
}


def test_emit_map_layout():
    text = emit_map(DOC)
    assert text.startswith("# Register map Dev\n#\n")
    body = text.split("\ndevice:\n", 1)[1]
    assert body == (
        "  name: Dev\n"
        "\n"
        "generator:\n"
        "  outputs:\n"
        "    reg_map: ../fw\n"
        "\n"
        "blocks:\n"
        "\n"
        "  SYS:\n"
        "    code: 0\n"
        "    name: SYSTEM\n"
        "    registers:\n"
        "      - name: STATUS\n"
        "        type: BIN\n"
        "        access: RO\n"
        "        size: 4\n"
        "        description: |-\n"
        "          Line one\n"
        "          Line two\n"
        "        bits:\n"
        '          - {name: S_ERR,         bit: 0,  label: Error, description: "Any, error"}\n'
        "          - {name: S_LONGER_NAME, bit: 17}\n"
        "\n"
        "      - name: MODE\n"
        "        type: ENUM\n"
        "        access: RWF\n"
        "        size: 1\n"
        "        default: M_B\n"
        "        modbus: {address: 100}\n"
        "        values:\n"
        '          - {name: M_A, label: "9600"}\n'
        "          - {name: M_B, value: 5}\n"
        "\n"
        "      - name: MAX\n"
        "        type: INT\n"
        "        access: RW\n"
        "        size: 2\n"
        "        max: 0x3FF\n"
        "        modbus: false\n"
    )


def test_emitted_map_loads_back():
    reg = load_map_text(emit_map(DOC)).blocks["SYS"].registers
    assert reg[0].description == "Line one\nLine two"
    assert reg[0].bits[0].description == "Any, error"
    assert reg[1].values[0].label == "9600"
    assert (reg[2].max, reg[2].modbus) == (1023, False)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `pytest -q tests/test_yaml_emit.py`
Expected: FAIL — `ModuleNotFoundError: No module named 'regmap.importers'`

- [ ] **Step 3: Implement**

Create `src/regmap/importers/__init__.py` with:

```python
"""Converters from legacy register map formats."""
```

Create `src/regmap/importers/yaml_emit.py` with:

```python
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
```

- [ ] **Step 4: Run the whole suite and the linters**

Run: `pytest -q && ruff check . && ruff format --check .`
Expected: `149 passed` (17 of them in this task's tests), `All checks passed!`, all files already formatted.

- [ ] **Step 5: Commit**

```bash
git add src/regmap/importers/__init__.py src/regmap/importers/yaml_emit.py tests/test_yaml_emit.py
git commit -m "feat: readable YAML writer for imported maps"
```

### Task 12: Excel import and the import-xlsx command

**Files:**
- Create: `src/regmap/importers/xlsx.py`
- Modify: `src/regmap/cli.py`
- Create: `doc/import-xlsx.md`
- Create: `doc/cli.md`
- Test: `tests/test_import_xlsx.py`

**Interfaces:**
- Consumes: `yaml_emit` (Task 11), `load_map_text`, `MapError` (Task 2), `resolve` (Task 3),
  `encode` (Task 8), `openpyxl`.
- Produces (`regmap.importers.xlsx`): `class ImportFailed(Exception)` with `errors`,
  `@dataclass(frozen=True) ImportResult(yaml_text: str, warnings: list[str],
  register_count: int)`, `import_workbook(workbook: Path, target: Path) -> ImportResult`;
  helpers `_number`, `_text`, `_vba_word_count`, `_destination` (tested directly).
  `regmap import-xlsx WORKBOOK [-o MAP] [--force]`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_import_xlsx.py`:

```python
from pathlib import Path

import pytest

openpyxl = pytest.importorskip("openpyxl")

from regmap.cli import main  # noqa: E402
from regmap.codes import Access, RegType, register_id  # noqa: E402
from regmap.importers.xlsx import (  # noqa: E402
    ImportFailed,
    _destination,
    _number,
    _text,
    _vba_word_count,
    import_workbook,
)
from regmap.model import load_map_text  # noqa: E402

HEADER = [
    "Block", "Name source", "Block Code", "Data Type", "Access Code", "Address", "Max length",
    "ID [hex]", "Name", "Label", "Factory\nvalue", "Minimal value", "Maximal value", "Units",
    "Modbus special", "Invalid", "Description",
]  # fmt: skip
CODES = {"SYS": 0, "COM": 3}


def reg(name, block, rtype, access, address, size, **cols):
    """A register row; the ID column is computed unless ``id`` is given."""
    rid = cols.pop("id", None)
    if rid is None:
        rid = f"0x{register_id(CODES[block], address, RegType(rtype), Access(access), size):08X}"
    return {
        "Name source": name, "Block Code": block, "Data Type": rtype, "Access Code": access,
        "Address": address, "Max length": size, "ID [hex]": rid, "Name": f"{block}_{name}",
        "Label": cols.get("label"), "Factory\nvalue": cols.get("default"),
        "Minimal value": cols.get("min"), "Maximal value": cols.get("max"),
        "Units": cols.get("unit"), "Modbus special": cols.get("modbus"),
        "Invalid": cols.get("invalid"), "Description": cols.get("description"),
    }  # fmt: skip


def item(name, label=None, override=None, description=None):
    return {"Name": name, "Label": label, "Factory\nvalue": override, "Description": description}


def make_workbook(path: Path, rows: list[dict], top: list[list] | None = None) -> Path:
    wb = openpyxl.Workbook()
    ws = wb.active
    ws.title = "Registers"
    for row in top or []:
        ws.append(row)
    ws.append([None, None, None, *HEADER])
    for row in rows:
        ws.append([None, None, None, None, *[row.get(h) for h in HEADER[1:]]])
    common = wb.create_sheet("Common")
    common.append([None, None, "Block ID"])
    common.append([None, None, "Abbrev.", "Code", "Name", "Description"])
    for abbrev, code in CODES.items():
        common.append([None, None, abbrev, code, f"{abbrev} name", f"{abbrev} description"])
    common.append([None, None, None, 7])
    wb.save(path)
    return path


def run(tmp_path: Path, rows: list[dict], top: list[list] | None = None):
    book = make_workbook(tmp_path / "Dev.xlsx", rows, top)
    result = import_workbook(book, tmp_path / "dev.yaml")
    return result, load_map_text(result.yaml_text)


def test_registers_items_and_addresses(tmp_path):
    rows = [
        reg("UPTIME", "SYS", "INT", "RO", 0, 4, label="Uptime", default=0, unit="s"),
        reg("STATUS", "SYS", "BIN", "RO", 4, 4, description="Flags  \nsecond line "),
        item("S_A", "A"),
        item("S_B", "B", override=16),
        item("S_C", "C"),
        reg("MODE", "SYS", "ENUM", "RWF", 12, 1, default=2, min=0, max=3),
        item("M_A"),
        item("M_B", override=2, description="two"),
        item("M_C"),
        {"Data Type": "stop"},
        reg("LIMIT", "SYS", "INT", "RW", 14, 2, max="0x3FF", default="0x10"),
    ]
    result, rmap = run(tmp_path, rows)
    assert result.warnings == []
    assert result.register_count == 4
    uptime, status, mode, limit = rmap.blocks["SYS"].registers
    assert (uptime.label, uptime.default, uptime.unit, uptime.address) == ("Uptime", 0, "s", None)
    assert status.description == "Flags\nsecond line"
    assert [(b.name, b.bit) for b in status.bits] == [("S_A", 0), ("S_B", 16), ("S_C", 17)]
    assert mode.address == 12  # gap after STATUS
    assert [(v.name, v.value) for v in mode.values] == [("M_A", None), ("M_B", 2), ("M_C", None)]
    assert (mode.default, mode.min, mode.max) == ("M_B", None, None)
    assert (limit.max, limit.default) == (1023, 16)
    assert "max: 0x3FF" in result.yaml_text
    assert rmap.blocks["SYS"].name == "SYS name"


def test_invalid_rows_and_stray_items_are_skipped_with_warnings(tmp_path):
    rows = [
        reg("OLD", "SYS", "ENUM", "RW", 0, 1, invalid="x"),
        item("OLD_A"),
        reg("A", "SYS", "INT", "RW", 1, 1),
        item("STRAY"),
    ]
    result, rmap = run(tmp_path, rows)
    assert [r.name for r in rmap.blocks["SYS"].registers] == ["A"]
    assert result.warnings == [
        "row 2: OLD is marked Invalid, skipped",
        "row 5: item STRAY under INT register ignored",
    ]


def test_modbus_specials_follow_vba_allocation(tmp_path):
    rows = [
        reg("I1", "SYS", "INT", "RO", 0, 2, modbus="HA 50"),  # moves the HOLD counter only
        reg("H1", "SYS", "INT", "RW", 2, 2),
        reg("H2", "SYS", "INT", "RW", 4, 2, modbus="HR 3"),  # skip 3 holding registers
        reg("X", "SYS", "INT", "RW", 6, 2, modbus="x"),
        reg("F", "SYS", "FLOAT", "RW", 8, 4, modbus="F32"),
        reg("Q", "SYS", "INT", "RW", 12, 2, modbus="??"),
    ]
    result, rmap = run(tmp_path, rows)
    modbus = {r.name: r.modbus for r in rmap.blocks["SYS"].registers}
    assert modbus["I1"] is None
    assert modbus["H1"].address == 50
    assert modbus["H2"].address == 54
    assert modbus["X"] is False
    assert (modbus["F"].address, modbus["F"].format) == (None, "F32")
    assert result.warnings == ["row 7: unknown Modbus special '??' ignored"]


def test_id_mismatch_fails(tmp_path):
    book = make_workbook(
        tmp_path / "Dev.xlsx", [reg("A", "SYS", "INT", "RO", 0, 4, id="0x00000999")]
    )
    with pytest.raises(ImportFailed) as exc:
        import_workbook(book, tmp_path / "dev.yaml")
    assert exc.value.errors == [
        "SYS_A: computed ID 0x00000112 differs from the sheet ID 0x00000999"
    ]


def test_unknown_block_fails(tmp_path):
    row = reg("A", "SYS", "INT", "RO", 0, 4) | {"Block Code": "ZZZ"}
    book = make_workbook(tmp_path / "Dev.xlsx", [row])
    with pytest.raises(ImportFailed, match="row 2: block 'ZZZ' is not in the Common sheet"):
        import_workbook(book, tmp_path / "dev.yaml")


def test_missing_column_fails(tmp_path):
    book = make_workbook(tmp_path / "Dev.xlsx", [reg("A", "SYS", "INT", "RO", 0, 4)])
    wb = openpyxl.load_workbook(book)
    wb["Registers"]["T1"] = None  # the Description header
    wb.save(book)
    with pytest.raises(ImportFailed, match="Registers sheet: missing column"):
        import_workbook(book, tmp_path / "dev.yaml")


def test_config_cells_and_destinations(tmp_path):
    top = [
        [None, None, None, "Config storage", None, "MY_REG"],
        [None, None, None, "RegMap destination", None, '=GetPath()&"\\..\\Firmware\\Core\\"'],
        [None, None, None, "Tests destination", None, "C:\\Users\\someone\\Tests\\"],
    ]
    result, rmap = run(tmp_path, [reg("A", "SYS", "INT", "RO", 0, 4)], top)
    assert rmap.device.c_storage == "MY_REG"
    assert rmap.device.c_prefix == "CONF_"
    assert rmap.generator.outputs.reg_map == "../Firmware/Core"
    assert rmap.generator.outputs.python is None
    assert result.warnings == ["'tests destination' is not a GetPath() formula, not imported"]


@pytest.mark.parametrize(
    ("cell", "expected"),
    [
        (None, None),
        ("", None),
        (4, 4),
        (4.0, 4),
        (1.5, 1.5),
        ("12", 12),
        ("1,5", 1.5),
        ("abc", None),
    ],
)
def test_number(cell, expected):
    assert _number(cell) == expected


def test_number_keeps_hex_text():
    assert _number(" 0x3FF ").text == "0x3FF"


def test_text():
    assert _text(None) is None
    assert _text("  ") is None
    assert _text(9600.0) == "9600"
    assert _text("a  \r\nb \n\n") == "a\nb"


@pytest.mark.parametrize(
    ("reg_type", "size", "f32", "words"),
    [("INT", 1, False, 1), ("INT", 4, False, 2), ("STRING", 3, False, 2), ("STRING", 5, False, 2),
     ("ENUM", 1, False, 1), ("FLOAT", 4, False, 1), ("FLOAT", 4, True, 2)],
)  # fmt: skip
def test_vba_word_count_uses_bankers_rounding(reg_type, size, f32, words):
    assert _vba_word_count(reg_type, size, f32) == words


def test_destination_formula(tmp_path):
    book_dir = tmp_path / "Documents"
    assert _destination('=GetPath()&"\\..\\Firmware\\"', book_dir, book_dir) == "../Firmware"
    assert _destination('=GetPath()&"\\..\\Firmware\\"', book_dir, tmp_path) == "Firmware"
    assert _destination("C:\\abs\\path", book_dir, book_dir) is None


def test_cli_import_refuses_to_overwrite(tmp_path, capsys):
    book = make_workbook(tmp_path / "Dev.xlsx", [reg("A", "SYS", "INT", "RO", 0, 4)])
    assert main(["import-xlsx", str(book)]) == 0
    assert (tmp_path / "dev.yaml").is_file()
    assert "written" in capsys.readouterr().out
    assert main(["import-xlsx", str(book)]) == 1
    assert "already exists; use --force" in capsys.readouterr().err
    assert main(["import-xlsx", str(book), "--force", "-o", str(tmp_path / "other.yaml")]) == 0
    assert main(["import-xlsx", str(tmp_path / "missing.xlsx")]) == 2


def test_items_after_a_stop_row_are_not_attached(tmp_path):
    rows = [
        reg("MODE", "SYS", "ENUM", "RW", 0, 1),
        item("M_A"),
        {"Data Type": "stop"},
        item("LOST"),
    ]
    result, rmap = run(tmp_path, rows)
    assert [v.name for v in rmap.blocks["SYS"].registers[0].values] == ["M_A"]
    assert result.warnings == ["row 5: item LOST has no register above it, ignored"]
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `pytest -q tests/test_import_xlsx.py`
Expected: FAIL — `ModuleNotFoundError: No module named 'regmap.importers.xlsx'`

- [ ] **Step 3: Implement**

Create `src/regmap/importers/xlsx.py` with:

```python
"""Import a legacy Excel/VBA register map workbook (e.g. Vms1511.xlsm) into YAML."""

import os
import re
import warnings
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import openpyxl

from regmap.importers.yaml_emit import HexInt, emit_map
from regmap.model import MapError, load_map_text
from regmap.resolve import resolve

REGISTER_COLUMNS = {
    "name_source": "name source",
    "block": "block code",
    "type": "data type",
    "access": "access code",
    "address": "address",
    "size": "max length",
    "id": "id [hex]",
    "name": "name",
    "label": "label",
    "default": "factory value",
    "min": "minimal value",
    "max": "maximal value",
    "unit": "units",
    "modbus": "modbus special",
    "invalid": "invalid",
    "description": "description",
}
BLOCK_COLUMNS = {"abbrev": "abbrev.", "code": "code", "name": "name", "description": "description"}
DESTINATIONS = {
    "regmap destination": ("reg_map",),
    "modbus destination": ("modbus",),
    "tests destination": ("lebin_json", "modbus_json", "python"),
}
INPUT_ACCESS = ("RO", "ROF")
_HEX = re.compile(r"^0[xX][0-9A-Fa-f]+$")
_GETPATH = re.compile(r'^=\s*GetPath\(\)\s*&\s*"(.*)"\s*$', re.IGNORECASE)
_IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


class ImportFailed(Exception):
    def __init__(self, errors: list[str]) -> None:
        super().__init__("\n".join(errors))
        self.errors = errors


@dataclass(frozen=True)
class ImportResult:
    yaml_text: str
    warnings: list[str]
    register_count: int


def _norm(value: Any) -> str:
    return " ".join(str(value).split()).lower() if value is not None else ""


def _text(value: Any) -> str | None:
    """Cell as text: lines right-trimmed, surrounding empty lines removed; None when empty."""
    if value is None:
        return None
    if isinstance(value, float) and value.is_integer():
        value = int(value)
    lines = str(value).replace("\r\n", "\n").replace("\r", "\n").split("\n")
    text = "\n".join(line.rstrip() for line in lines).strip("\n")
    return text if text.strip() else None


def _number(value: Any) -> int | float | None:
    """Cell as number (hex text kept as HexInt); None when empty or not a number."""
    if value is None or isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, float):
        return int(value) if value.is_integer() else value
    text = str(value).strip()
    if _HEX.match(text):
        return HexInt(text)
    for convert in (int, lambda t: float(t.replace(",", "."))):
        try:
            return convert(text)
        except ValueError:
            pass
    return None


def _open(path: Path, data_only: bool):
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")  # openpyxl warns about unsupported Excel extensions
        return openpyxl.load_workbook(path, read_only=True, data_only=data_only)


def _rows(sheet) -> list[tuple[Any, ...]]:
    return [tuple(row) for row in sheet.iter_rows(values_only=True)]


def _columns(header: tuple[Any, ...], wanted: dict[str, str], where: str) -> dict[str, int]:
    found = {_norm(v): i for i, v in reversed(list(enumerate(header))) if v is not None}
    missing = [text for text in wanted.values() if text not in found]
    if missing:
        raise ImportFailed([f"{where}: missing column(s): {', '.join(missing)}"])
    return {key: found[text] for key, text in wanted.items()}


def _cell(row: tuple[Any, ...], index: int) -> Any:
    return row[index] if index < len(row) else None


def _label_value(rows: list[tuple[Any, ...]], label: str) -> Any:
    """Value of the first non-empty cell right of a cell whose text is ``label``."""
    for row in rows:
        for i, value in enumerate(row):
            if _norm(value) == label:
                return next((v for v in row[i + 1 :] if v not in (None, "")), None)
    return None


def _read_blocks(sheet) -> dict[str, dict[str, Any]]:
    rows = _rows(sheet)
    start = next((i for i, r in enumerate(rows) if any(_norm(v) == "block id" for v in r)), None)
    if start is None:
        raise ImportFailed(["Common sheet: 'Block ID' table not found"])
    head = next(i for i in range(start, len(rows)) if any(_norm(v) == "abbrev." for v in rows[i]))
    cols = _columns(rows[head], BLOCK_COLUMNS, "Common sheet")
    blocks = {}
    for row in rows[head + 1 :]:
        code = _number(_cell(row, cols["code"]))
        if code is None:
            break
        abbrev = _text(_cell(row, cols["abbrev"]))
        if abbrev:
            blocks[abbrev] = {
                "code": int(code),
                "name": _text(_cell(row, cols["name"])),
                "description": _text(_cell(row, cols["description"])),
            }
    return blocks


def _vba_word_count(reg_type: str, size: int, f32: bool) -> int:
    """Modbus words as the VBA macro counted them (banker's rounding of size / 2)."""
    if reg_type in ("INT", "STRING", "BIN"):
        return max(round(size / 2), 1)
    if reg_type == "FLOAT":
        return 2 if f32 else 1
    return 1


def _word_count(reg_type: str, size: int, f32: bool) -> int:
    if reg_type == "ENUM":
        return 1
    if reg_type == "FLOAT":
        return 2 if f32 else 1
    return (size + 1) // 2


def _destination(formula: Any, workbook_dir: Path, yaml_dir: Path) -> str | None:
    match = _GETPATH.match(str(formula).strip())
    if not match:
        return None
    relative = match.group(1).replace("\\", "/").strip("/")
    target = os.path.normpath(workbook_dir / relative)
    try:
        return Path(os.path.relpath(target, yaml_dir)).as_posix()
    except ValueError:  # different drive on Windows
        return None


class _Importer:
    def __init__(self, workbook: Path, target: Path) -> None:
        self.workbook = workbook
        self.target = target
        self.warnings: list[str] = []
        self.errors: list[str] = []
        self.excel_ids: dict[str, int] = {}
        self.vba_modbus: dict[str, tuple[str, int, int]] = {}  # full name -> (space, start, count)
        self.vba_counter = {"INPUT": 0, "HOLD": 0}

    def run(self) -> ImportResult:
        values = _open(self.workbook, data_only=True)
        formulas = _open(self.workbook, data_only=False)
        for name in ("Registers", "Common"):
            if name not in values.sheetnames:
                raise ImportFailed([f"sheet '{name}' not found"])
        rows = _rows(values["Registers"])
        header = next(
            (i for i, r in enumerate(rows) if any(_norm(v) == "block code" for v in r)), None
        )
        if header is None:
            raise ImportFailed(["Registers sheet: header row with 'Block Code' not found"])
        cols = _columns(rows[header], REGISTER_COLUMNS, "Registers sheet")
        known_blocks = _read_blocks(values["Common"])
        registers = self._read_registers(rows, header, cols, known_blocks)
        if self.errors:
            raise ImportFailed(self.errors)
        doc = {
            "device": self._device(rows[:header]),
            "generator": self._generator(_rows(formulas["Registers"])[:header]),
            "blocks": self._blocks(registers, known_blocks),
        }
        text = emit_map(doc)
        self._verify(text)
        return ImportResult(text, self.warnings, sum(len(r) for r in registers.values()))

    # -- sheet rows -> plain register dicts

    def _read_registers(self, rows, header, cols, known_blocks) -> dict[str, list[dict[str, Any]]]:
        registers: dict[str, list[dict[str, Any]]] = {}
        offsets: dict[str, int] = {}
        current: dict[str, Any] | None = None
        skipping = False  # items of a skipped (Invalid) register are dropped silently
        for number, row in enumerate(rows[header + 1 :], start=header + 2):

            def get(key: str, row=row) -> Any:
                return _cell(row, cols[key])

            special = _text(get("modbus")) or ""
            self._vba_special(special)
            reg_type = _text(get("type"))
            name_source = _text(get("name_source"))
            if reg_type:  # a register row, or a separator such as "stop"
                current, skipping = None, False
                if not name_source:
                    continue
                if _text(get("invalid")):
                    self.warnings.append(f"row {number}: {name_source} is marked Invalid, skipped")
                    skipping = True
                    continue
                block = _text(get("block"))
                if block not in known_blocks:
                    self.errors.append(f"row {number}: block {block!r} is not in the Common sheet")
                    continue
                current = self._register(
                    number, get, block, reg_type, name_source, special, offsets
                )
                if current is not None:
                    registers.setdefault(block, []).append(current)
            elif _text(get("name")):
                if current is not None:
                    self._item(number, get, current)
                elif not skipping:
                    self.warnings.append(
                        f"row {number}: item {_text(get('name'))} has no register above it, ignored"
                    )
        for regs in registers.values():
            for reg in regs:
                self._finish_items(reg)
        return registers

    def _vba_special(self, special: str) -> None:
        parts = special.split()
        if len(parts) == 2 and parts[0] in ("IA", "HA", "IR", "HR") and parts[1].isdigit():
            space = "INPUT" if parts[0][0] == "I" else "HOLD"
            n = int(parts[1])
            self.vba_counter[space] = n if parts[0][1] == "A" else self.vba_counter[space] + n

    def _register(self, number, get, block, reg_type, name, special, offsets):
        full = f"{block}_{name}"
        size = _number(get("size"))
        address = _number(get("address"))
        if size is None or address is None:
            self.errors.append(f"row {number}: {full} needs Address and Max length")
            return None
        size, address = int(size), int(address)
        access = _text(get("access")) or ""
        reg: dict[str, Any] = {"name": name, "type": reg_type, "access": access, "size": size}
        offset = offsets.get(block, 0)
        if address != offset:
            reg["address"] = address
        offsets[block] = max(offset, address + size)
        label = _text(get("label"))
        if label and "\n" in label:
            self.warnings.append(f"row {number}: multi-line label of {full} joined into one line")
            label = " ".join(label.split("\n"))
        if label:
            reg["label"] = label
        raw_default = get("default")
        if reg_type == "STRING":
            if _text(raw_default):
                reg["default"] = _text(raw_default)
        elif raw_default not in (None, ""):
            value = _number(raw_default)
            if value is None:
                self.warnings.append(f"row {number}: default of {full} is not a number, dropped")
            else:
                reg["default"] = value
        for key in ("min", "max"):
            value = _number(get(key))
            if value is not None:
                reg[key] = value
        unit = _text(get("unit"))
        if unit:
            reg["unit"] = unit
        f32 = special == "F32"
        if special.lower() == "x":
            reg["modbus"] = False
        elif f32 and reg_type != "FLOAT":
            self.warnings.append(f"row {number}: F32 on non-FLOAT register {full} ignored")
            f32 = False
        elif f32:
            reg["modbus"] = {"format": "F32"}
        elif special and not re.match(r"^(IA|HA|IR|HR) \d+$", special):
            self.warnings.append(f"row {number}: unknown Modbus special {special!r} ignored")
        description = _text(get("description"))
        if description:
            reg["description"] = description
        reg["_items"] = []
        reg["_row"] = number
        excel_id = _text(get("id"))
        if excel_id and _HEX.match(excel_id):
            self.excel_ids[full] = int(excel_id, 16)
        if reg.get("modbus") is not False:
            space = "INPUT" if access in INPUT_ACCESS else "HOLD"
            count = _vba_word_count(reg_type, size, f32)
            self.vba_modbus[full] = (space, self.vba_counter[space], count)
            self.vba_counter[space] += count
        return reg

    def _item(self, number, get, reg) -> None:
        if reg["type"] not in ("ENUM", "BIN"):
            self.warnings.append(
                f"row {number}: item {_text(get('name'))} under {reg['type']} register ignored"
            )
            return
        reg["_items"].append(
            {
                "name": _text(get("name")),
                "label": _text(get("label")),
                "description": _text(get("description")),
                "override": _number(get("default")),
            }
        )

    def _finish_items(self, reg: dict[str, Any]) -> None:
        items = reg.pop("_items")
        row = reg.pop("_row")
        running = 0
        converted = []
        for item in items:
            number = running if item["override"] is None else int(item["override"])
            entry = {"name": item["name"]}
            if reg["type"] == "BIN":
                entry["bit"] = number
            elif number != running:
                entry["value"] = number
            entry["label"] = item["label"]
            entry["description"] = item["description"]
            converted.append((entry, number))
            running = number + 1
        if reg["type"] == "BIN" and converted:
            reg["bits"] = [e for e, _ in converted]
        if reg["type"] != "ENUM":
            return
        reg["values"] = [e for e, _ in converted]
        numbers = [n for _, n in converted]
        full = f"row {row}: {reg['name']}"
        if "default" in reg:
            by_number = {n: e["name"] for e, n in converted}
            if reg["default"] in by_number:
                reg["default"] = by_number[reg["default"]]
            else:
                self.warnings.append(f"{full}: default {reg['default']} is not a value, dropped")
                del reg["default"]
        sheet_range = (reg.pop("min", None), reg.pop("max", None))
        if numbers and sheet_range != (None, None) and sheet_range != (min(numbers), max(numbers)):
            self.warnings.append(
                f"{full}: sheet min/max {sheet_range} differ from the values, the values win"
            )

    # -- document parts

    def _device(self, top_rows) -> dict[str, Any]:
        name = self.workbook.stem
        if not _IDENT.match(name):
            raise ImportFailed([f"workbook name {name!r} is not a valid device name"])
        device: dict[str, Any] = {"name": name}
        prefix = _text(_label_value(top_rows, "config c prefix"))
        storage = _text(_label_value(top_rows, "config storage"))
        if prefix and prefix != "CONF_":
            device["c_prefix"] = prefix
        if storage and storage != "CONF_REG":
            device["c_storage"] = storage
        return device

    def _generator(self, top_rows) -> dict[str, Any]:
        outputs: dict[str, str] = {}
        workbook_dir = self.workbook.resolve().parent
        yaml_dir = self.target.resolve().parent
        for label, keys in DESTINATIONS.items():
            formula = _label_value(top_rows, label)
            if formula is None:
                continue
            path = _destination(formula, workbook_dir, yaml_dir)
            if path is None:
                self.warnings.append(f"'{label}' is not a GetPath() formula, not imported")
                continue
            for key in keys:
                outputs[key] = path
        return {"outputs": outputs} if outputs else {}

    def _blocks(self, registers, known_blocks) -> dict[str, Any]:
        self._modbus_overrides(registers, known_blocks)
        blocks = {}
        for abbrev in sorted(registers, key=lambda a: known_blocks[a]["code"]):
            info = known_blocks[abbrev]
            block = {"code": info["code"], "name": info["name"], "description": info["description"]}
            blocks[abbrev] = {
                **{k: v for k, v in block.items() if v is not None},
                "registers": registers[abbrev],
            }
        return blocks

    def _modbus_overrides(self, registers, known_blocks) -> None:
        """Pin Modbus start addresses wherever automatic allocation would differ from VBA."""
        counter = {"INPUT": 0, "HOLD": 0}
        for abbrev in sorted(registers, key=lambda a: known_blocks[a]["code"]):
            for reg in registers[abbrev]:
                vba = self.vba_modbus.get(f"{abbrev}_{reg['name']}")
                if vba is None:
                    continue
                space, start, _ = vba
                if counter[space] != start:
                    options = reg["modbus"] if isinstance(reg.get("modbus"), dict) else {}
                    reg["modbus"] = {"address": start, **options}
                    counter[space] = start
                f32 = isinstance(reg.get("modbus"), dict) and reg["modbus"].get("format") == "F32"
                counter[space] += _word_count(reg["type"], reg["size"], f32)

    def _verify(self, text: str) -> None:
        try:
            resolved = resolve(load_map_text(text, str(self.target)))
        except MapError as exc:
            raise ImportFailed(["the converted map is not valid:"] + exc.errors) from None
        for reg in resolved.registers:
            expected = self.excel_ids.get(reg.full_name)
            if expected is not None and expected != reg.id:
                self.errors.append(
                    f"{reg.full_name}: computed ID 0x{reg.id:08X}"
                    f" differs from the sheet ID 0x{expected:08X}"
                )
            vba = self.vba_modbus.get(reg.full_name)
            if (
                vba
                and reg.modbus
                and (reg.modbus.addresses[0], len(reg.modbus.addresses)) != vba[1:]
            ):
                self.warnings.append(
                    f"{reg.full_name}: Modbus words {list(reg.modbus.addresses)} differ from the"
                    f" VBA allocation (start {vba[1]}, {vba[2]} words)"
                )
        if self.errors:
            raise ImportFailed(self.errors)


def import_workbook(workbook: Path, target: Path) -> ImportResult:
    """Convert ``workbook``; ``target`` is where the YAML will be written (for relative paths)."""
    return _Importer(workbook, target).run()
```

Replace `src/regmap/cli.py` with:

```python
"""Command line interface: regmap generate | import-xlsx | schema."""

import argparse
import json
import sys
from collections.abc import Sequence
from pathlib import Path

from regmap import __version__
from regmap.check import PreviousOutputError, breaking_changes, format_changes
from regmap.model import MapError, RegisterMap, load_map
from regmap.outputs import render_outputs
from regmap.resolve import resolve
from regmap.templating import TemplateError
from regmap.writer import encode, stale, write


def _error(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)


def _generate(args: argparse.Namespace) -> int:
    path: Path = args.map
    if not path.is_file():
        _error(f"{path}: file not found")
        return 2
    try:
        rmap = load_map(path)
        resolved = resolve(rmap)
        outputs = render_outputs(resolved, rmap.generator, path.parent, args.out)
    except MapError as exc:
        _error(f"{path}: {len(exc.errors)} problem(s)")
        for message in exc.errors:
            print(f"  {message}", file=sys.stderr)
        return 1
    except TemplateError as exc:
        _error(str(exc))
        return 1

    if args.check:
        outdated = stale(outputs)
        for p in outdated:
            print(f"outdated  {p}")
        return 1 if outdated else 0

    try:
        changes = breaking_changes(outputs)
    except PreviousOutputError as exc:
        if not args.allow_id_change:
            _error(f"{exc}; use --allow-id-change to overwrite it")
            return 1
        changes = []
    if changes:
        table = format_changes(changes)
        if not args.allow_id_change:
            _error(f"breaking changes against the previous outputs, nothing written:\n{table}")
            print("use --allow-id-change if the change is intended", file=sys.stderr)
            return 1
        print(f"warning: breaking changes accepted (--allow-id-change):\n{table}", file=sys.stderr)

    for p, status in write(outputs):
        print(f"{status:9} {p}")
    return 0


def _schema(args: argparse.Namespace) -> int:
    text = json.dumps(RegisterMap.model_json_schema(), indent=2) + "\n"
    if args.output is None:
        sys.stdout.write(text)
    else:
        args.output.write_bytes(encode(text))
    return 0


def _import_xlsx(args: argparse.Namespace) -> int:
    try:
        from regmap.importers.xlsx import ImportFailed, import_workbook
    except ImportError:
        _error('import-xlsx needs openpyxl: pip install "py-reg-map[xlsx]"')
        return 1
    workbook: Path = args.workbook
    if not workbook.is_file():
        _error(f"{workbook}: file not found")
        return 2
    target: Path = args.output or workbook.with_name(workbook.stem.lower() + ".yaml")
    if target.exists() and not args.force:
        _error(f"{target} already exists; use --force to overwrite it")
        return 1
    try:
        result = import_workbook(workbook, target)
    except ImportFailed as exc:
        _error(f"{workbook}: import failed")
        for message in exc.errors:
            print(f"  {message}", file=sys.stderr)
        return 1
    target.write_bytes(encode(result.yaml_text))
    for warning in result.warnings:
        print(f"warning: {warning}", file=sys.stderr)
    print(
        f"written   {target} ({result.register_count} registers, {len(result.warnings)} warnings)"
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="regmap", description="Register map generator")
    parser.add_argument("--version", action="version", version=f"regmap {__version__}")
    sub = parser.add_subparsers(dest="command", required=True)

    gen = sub.add_parser("generate", help="generate C and JSON outputs from a YAML map")
    gen.add_argument("map", type=Path, help="register map YAML file")
    gen.add_argument("--out", type=Path, help="write all outputs to this directory")
    gen.add_argument("--allow-id-change", action="store_true", help="accept breaking changes")
    gen.add_argument("--check", action="store_true", help="only check that outputs are current")
    gen.set_defaults(func=_generate)

    imp = sub.add_parser("import-xlsx", help="convert a legacy Excel register map to YAML")
    imp.add_argument("workbook", type=Path, help="Excel workbook (.xlsm/.xlsx)")
    imp.add_argument("-o", "--output", type=Path, help="YAML file to write")
    imp.add_argument("--force", action="store_true", help="overwrite an existing YAML file")
    imp.set_defaults(func=_import_xlsx)

    sch = sub.add_parser("schema", help="print the JSON Schema of the YAML format")
    sch.add_argument("-o", "--output", type=Path, help="write the schema to this file")
    sch.set_defaults(func=_schema)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)
```

- [ ] **Step 4: Run the whole suite and the linters**

Run: `pytest -q && ruff check . && ruff format --check .`
Expected: `176 passed` (27 of them in this task's tests), `All checks passed!`, all files already formatted.

- [ ] **Step 5: Documentation**

Create `doc/import-xlsx.md`:

````markdown
# Importing an Excel register map

`regmap import-xlsx` converts a workbook of the former VBA generator (e.g. `Vms1511.xlsm`)
into a YAML map. It needs the `xlsx` extra: `pip install "py-reg-map[xlsx] @ git+…"`.

```sh
regmap import-xlsx Documents/Vms1511.xlsm            # writes Documents/vms1511.yaml
regmap import-xlsx Vms1511.xlsm -o map/vms1511.yaml --force
```

The workbook must have been saved by Excel (the importer reads the cached cell values).

## Expected workbook layout

- Sheet **Registers** with a header row containing `Name source`, `Block Code`, `Data Type`,
  `Access Code`, `Address`, `Max length`, `ID [hex]`, `Name`, `Label`, `Factory value`,
  `Minimal value`, `Maximal value`, `Units`, `Modbus special`, `Invalid`, `Description`.
  Columns are found by their header text, so their position does not matter.
- Cells labelled `Config storage`, `Config C prefix`, `RegMap destination`,
  `Modbus destination`, `Tests destination` above the header (value in the first non-empty
  cell to the right).
- Sheet **Common** with the `Block ID` table (`Abbrev.`, `Code`, `Name`, `Description`).

## Rows

- A row with `Data Type` and `Name source` is a register.
- Rows below it without `Data Type` but with `Name` are its enum values (ENUM) or bits (BIN).
  For other types they are ignored with a warning.
- A row with `Data Type` but without `Name source` (e.g. `stop`) ends the item list; later
  items without a register are ignored with a warning.
- Registers with `Invalid` set are skipped (with a warning) together with their items.
- Empty rows are ignored.

## Conversion

| sheet | YAML |
|---|---|
| `Address` | `address`, only when it differs from the automatic address |
| `Factory value` of an ENUM | `default` as the value name |
| `Factory value` of an enum item | `value`, when it differs from previous + 1 |
| `Factory value` of a bit | the bit number; bits always get an explicit `bit` |
| `Minimal`/`Maximal value` of an ENUM | dropped (derived); a warning when they differ from the values |
| `Modbus special` `x` | `modbus: false` |
| `Modbus special` `HA n` / `IA n` / `HR n` / `IR n` | `modbus: {address: …}` wherever the automatic allocation would differ from the VBA allocation |
| `Modbus special` `F32` | `modbus: {format: F32}` (FLOAT only) |
| hex texts such as `0x3FF` | kept in hex |
| multi-line descriptions | `\|-` blocks, trailing spaces removed |
| multi-line labels | joined into one line (warning) |
| workbook file name | `device.name` |
| `Config C prefix` / `Config storage` | `c_prefix` / `c_storage`, only when not the default |
| `RegMap` / `Modbus` / `Tests destination` | `generator.outputs` (`reg_map`, `modbus`, `lebin_json` + `modbus_json` + `python`), only for `=GetPath()&"\…"` formulas, converted to paths relative to the YAML file |

## Verification

The produced YAML is loaded and resolved. Every computed register ID must equal the sheet's
`ID [hex]`; any difference stops the import (nothing is written). Modbus addresses that differ
from the VBA allocation are reported as warnings. A summary of all warnings is printed at the
end.
````

Create `doc/cli.md`:

````markdown
# Command line

```
regmap generate MAP.yaml [--out DIR] [--allow-id-change] [--check]
regmap import-xlsx WORKBOOK.xlsm [-o MAP.yaml] [--force]
regmap schema [-o FILE]
regmap --version
```

## `generate`

1. Loads and validates the map; all problems are printed at once.
2. Renders all seven outputs in memory (templates from `generator.templates`, otherwise the
   package defaults — see [templates.md](templates.md)).
3. Destinations: `generator.outputs.<key>` relative to the YAML file, otherwise the YAML
   file's directory. `--out DIR` writes everything to `DIR` instead.
4. Runs the [stability check](stability-check.md) against the previous JSON outputs.
5. Writes only files whose content changed and prints `written` or `unchanged` per file
   (unchanged files keep their timestamps, so the firmware is not rebuilt needlessly).

Options:

| option | effect |
|---|---|
| `--out DIR` | all outputs into `DIR`, `generator.outputs` ignored |
| `--allow-id-change` | accept breaking ID / Modbus changes (printed as a warning) |
| `--check` | write nothing; print `outdated <file>` for every missing or different output and exit 1 if there is any — for CI pipelines that verify the committed outputs match the map |

## `import-xlsx`

Converts a legacy Excel workbook into a YAML map, see [import-xlsx.md](import-xlsx.md).
`-o` defaults to `<workbook name in lower case>.yaml` next to the workbook; an existing file
is overwritten only with `--force`. Requires the `xlsx` extra.

## `schema`

Prints the JSON Schema of the YAML format, or writes it to `FILE` (see
[yaml-format.md](yaml-format.md#editor-support)).

## Exit codes

| code | meaning |
|---|---|
| 0 | success |
| 1 | invalid map, template error, breaking change, `--check` found differences, import failed |
| 2 | bad arguments or input file not found |
````

- [ ] **Step 6: Commit**

```bash
git add src/regmap/importers/xlsx.py src/regmap/cli.py doc/import-xlsx.md doc/cli.md tests/test_import_xlsx.py
git commit -m "feat: import legacy Excel register maps"
```

### Task 13: VMS-1511 reference outputs

**Files:**
- Add: `example/` (existing reference files) and `example/vms1511.yaml` (generated)
- Create: `doc/differences-from-vba.md`
- Test: `tests/test_reference_vms1511.py`

**Interfaces:**
- Consumes: everything above; `example/` reference files.
- Produces: committed `example/` (workbook, VBA outputs, `vms1511.yaml`) and the reference
  test that keeps the generator byte-compatible.

- [ ] **Step 1: Generate the YAML map from the reference workbook**

The eight files in `example/` (uploaded by the user, untracked so far) are the reference:
`Vms1511.xlsm`, `reg_map.h`, `reg_map.c`, `mb_rtu_app.h`, `mb_rtu_app.c`,
`Vms1511_registers.json`, `Vms1511_Modbus.json`, `Vms1511Regs.py`. Do not modify them.

```bash
regmap import-xlsx example/Vms1511.xlsm
python -c "import hashlib; print(hashlib.sha256(open('example/vms1511.yaml','rb').read()).hexdigest())"
```

Expected: `written   example/vms1511.yaml (26 registers, 0 warnings)` (the path separator depends on
the OS) and the hash `feab867fbe60cb23d27ae1cf83a547b343b47398b714d85841ad6fbbd94667a1`.

- [ ] **Step 2: Write the tests**

Create `tests/test_reference_vms1511.py`:

```python
"""VMS-1511: outputs generated from example/vms1511.yaml against the VBA reference files."""

import json

import pytest
from helpers import EXAMPLE

from regmap.generators import lebin_json, modbus_json
from regmap.model import load_map
from regmap.outputs import render_outputs
from regmap.resolve import resolve
from regmap.writer import encode

LEGEND_HEADERS = ("Allowed values: \r\n", "Meaning of respective bits: \r\n")


@pytest.fixture(scope="module")
def generated(tmp_path_factory):
    rmap = load_map(EXAMPLE / "vms1511.yaml")
    out = tmp_path_factory.mktemp("vms1511")
    return {o.path.name: o for o in render_outputs(resolve(rmap), rmap.generator, EXAMPLE, out)}


@pytest.mark.parametrize("name", ["reg_map.h", "reg_map.c", "mb_rtu_app.h", "mb_rtu_app.c"])
def test_c_files_are_byte_identical(generated, name):
    assert encode(generated[name].text) == (EXAMPLE / name).read_bytes()


def test_python_file_is_identical_except_bom(generated):
    reference = (EXAMPLE / "Vms1511Regs.py").read_bytes().removeprefix(b"\xef\xbb\xbf")
    assert encode(generated["Vms1511Regs.py"].text) == reference


def apply_documented_differences(description: str) -> str:
    """doc/differences-from-vba.md items 2 and 15 applied to a VBA description."""
    text, legend = description, ""
    for header in LEGEND_HEADERS:
        if header in description:
            text, legend = description.split(header, 1)
            legend = header + legend.replace(" - .\r\n", ".\r\n")
            text = text.removesuffix("\r\n")
    text = "\r\n".join(line.rstrip() for line in text.split("\r\n"))
    return (text + "\r\n" if text and legend else text) + legend


@pytest.mark.parametrize(
    ("name", "render", "encoding"),
    [
        ("Vms1511_registers.json", lebin_json.render, "cp1250"),
        ("Vms1511_Modbus.json", modbus_json.render, "utf-8-sig"),
    ],
)
def test_json_files_match_after_documented_differences(name, render, encoding):
    reference = json.loads((EXAMPLE / name).read_bytes().decode(encoding))
    for entry in reference:
        entry.pop("IsVisible", None)  # difference 1
        entry.pop("ConfigUser", None)
        entry["Description"] = apply_documented_differences(entry["Description"])
    generated = json.loads(render(resolve(load_map(EXAMPLE / "vms1511.yaml"))))
    assert generated == reference
    assert [list(e) for e in generated] == [list(e) for e in reference]  # key order


def test_import_reproduces_the_committed_yaml():
    pytest.importorskip("openpyxl")
    from regmap.importers.xlsx import import_workbook

    result = import_workbook(EXAMPLE / "Vms1511.xlsm", EXAMPLE / "vms1511.yaml")
    assert result.warnings == []
    assert encode(result.yaml_text) == (EXAMPLE / "vms1511.yaml").read_bytes()
```

- [ ] **Step 3: Run the whole suite and the linters**

Run: `pytest -q && ruff check . && ruff format --check .`
Expected: `184 passed` (8 of them in this task's tests), `All checks passed!`, all files already formatted.

- [ ] **Step 4: Documentation**

Create `doc/differences-from-vba.md`:

```markdown
# Differences from the VBA outputs

The outputs keep the C API and the JSON structure of the former Excel/VBA generator, so
firmware and communication software need no changes. These differences are intentional:

1. LeBin JSON: the keys `IsVisible` (always `true`) and `ConfigUser` (set by accident from
   the `Modbus special` column) are removed.
2. Legends in descriptions: an item without description is written as `<label>.` instead of
   `<label> - .`; the legend does not start with an empty line when the register has no
   description; an item without label uses its name; a BIN register without bits gets no
   legend header.
3. Encoding: all files are UTF-8 without BOM (the LeBin JSON was cp1250, the Modbus JSON and
   the Python file had a BOM).
4. JSON whitespace follows `json.dumps(indent=2)` instead of hand-made formatting.
5. C enum typedefs use explicit enum values (VBA numbered 0..n−1 in C but honored the values
   in JSON).
6. Modbus word count is `ceil(size / 2)` (VBA used banker's rounding, 5 bytes → 2 words).
7. C members of sizes other than 1/2/4/8 bytes are always arrays (VBA: only above 8 or 3).
8. `FACT_SERIAL_NUMBER` enters the calibration list only when the map has it.
9. Modbus JSON `Value` of a STRING register is a JSON string (VBA wrote invalid JSON).
10. `c_prefix` is used everywhere (VBA hard-coded `CONF_` in the Modbus code and the
    flash/calibration lists).
11. ENUM `Min`/`Max` in Modbus JSON come from the values (VBA: sheet columns).
12. Factory values are aligned by the width of all names (VBA: names with a factory value
    only); hex defaults are printed in decimal.
13. All outputs order blocks by `code` (VBA used sheet order except for C structures).
14. `MB_*_LAST` is the highest allocated address (VBA: counter − 1; equal unless an explicit
    address jumps backwards).
15. Descriptions lose trailing spaces at line ends (the importer removes them).

For VMS-1511 only differences 1–4 and 15 are visible; `tests/test_reference_vms1511.py`
applies exactly these to the VBA files and requires everything else to match.
```

- [ ] **Step 5: Commit**

```bash
git add doc/differences-from-vba.md tests/test_reference_vms1511.py example
git commit -m "test: VMS-1511 reference outputs and imported map"
```

### Task 14: README and documentation index

**Files:**
- Modify: `README.md`
- Create: `doc/index.md`
- Create: `doc/development.md`

**Interfaces:**
- Consumes: all commands (for the usage section).
- Produces: final `README.md`, `doc/index.md`, `doc/development.md`.

- [ ] **Step 1: Implement**

Replace `README.md` with:

````markdown
# py-reg-map

Register map generator for embedded firmware. A YAML file describes the device registers;
`regmap` generates the C storage and Modbus RTU sources for the firmware (`reg_map.h/.c`,
`mb_rtu_app.h/.c`) and the JSON definitions for the LeBin and Modbus RTU communication
software.

## Installation

Python 3.12 or newer:

```sh
pip install "py-reg-map @ git+https://github.com/LogicElements/py-reg-map"
pip install "py-reg-map[xlsx] @ git+https://github.com/LogicElements/py-reg-map"   # with Excel import
```

## Usage

Migrate an existing Excel register map (once):

```sh
regmap import-xlsx Documents/Vms1511.xlsm        # -> Documents/vms1511.yaml
```

Generate all outputs after editing the map, then commit them:

```sh
regmap generate Documents/vms1511.yaml
regmap generate Documents/vms1511.yaml --check              # CI: are the committed outputs current?
regmap generate Documents/vms1511.yaml --allow-id-change    # accept intended ID/Modbus changes
```

Editor completion for the YAML format:

```sh
regmap schema -o Documents/regmap.schema.json
```

Documentation: [doc/index.md](doc/index.md).
````

- [ ] **Step 2: Documentation**

Create `doc/index.md`:

````markdown
# py-reg-map documentation

`regmap` generates the register map of an embedded device from one YAML file:

```
vms1511.yaml --load + validate--> model --resolve--> derived values --generators--> outputs
                                                                     reg_map.h/.c, mb_rtu_app.h/.c,
                                                                     <name>_registers.json (LeBin),
                                                                     <name>_Modbus.json, <name>Regs.py
```

| page | content |
|---|---|
| [yaml-format.md](yaml-format.md) | The YAML map: keys, rules, error messages, editor support. |
| [derived-values.md](derived-values.md) | Names, addresses, register IDs, C types, Modbus allocation, flash/calibration lists. |
| [outputs.md](outputs.md) | Every generated file and its format. |
| [templates.md](templates.md) | Default templates, project templates, placeholders. |
| [cli.md](cli.md) | Commands, options, destinations, exit codes. |
| [stability-check.md](stability-check.md) | Protection against changed register IDs and Modbus addresses. |
| [import-xlsx.md](import-xlsx.md) | Migrating an Excel/VBA register map. |
| [differences-from-vba.md](differences-from-vba.md) | Intentional differences from the former VBA outputs. |
| [development.md](development.md) | Development setup, layout, tests, CI. |
| [design/](design/) | Design specs and implementation plans. |
````

Create `doc/development.md`:

````markdown
# Development

## Setup

```sh
python -m venv .venv
.venv/Scripts/activate        # Windows; on Linux/macOS: source .venv/bin/activate
pip install -e ".[dev]"
```

The `dev` extra installs `pytest`, `ruff` and `openpyxl`.

## Checks

```sh
ruff check .
ruff format --check .
pytest
```

GitHub Actions (`.github/workflows/ci.yml`) runs the same three commands on Python 3.12, 3.13
and 3.14 for every push and pull request.

## Project layout

| path | responsibility |
|---|---|
| `src/regmap/codes.py` | Data types, access types, register ID composition, block count. |
| `src/regmap/model.py` | pydantic models of the YAML format, per-object validation, YAML loading. |
| `src/regmap/resolve.py` | Derived values (addresses, IDs, C types, Modbus allocation) and cross-object checks. |
| `src/regmap/templating.py` | Template lookup (project directory first) and placeholder filling. |
| `src/regmap/templates/` | Default C templates shipped with the package. |
| `src/regmap/generators/` | One module per output; they only read the resolved map. |
| `src/regmap/outputs.py` | Renders all output files in memory and decides their destinations. |
| `src/regmap/writer.py` | CRLF + UTF-8 writing, unchanged files are not rewritten, `--check` comparison. |
| `src/regmap/check.py` | Stability check against the previous JSON outputs. |
| `src/regmap/cli.py` | `regmap` command line. |
| `src/regmap/importers/` | Excel import (`xlsx.py`) and the readable YAML writer (`yaml_emit.py`). |
| `example/` | VMS-1511 workbook, its VBA outputs (reference) and `vms1511.yaml`. |
| `doc/design/` | Design specs and implementation plans. |

## Tests

- Unit tests per module in `tests/test_<module>.py`; `tests/helpers.py` builds small maps from
  inline YAML.
- `tests/test_reference_vms1511.py` generates `example/vms1511.yaml` and compares with the VBA
  outputs in `example/`: C files byte-for-byte, JSON after the documented differences
  (`doc/differences-from-vba.md`). It also checks that importing `example/Vms1511.xlsm`
  reproduces `example/vms1511.yaml` exactly.

## Line endings

`.gitattributes` stores `example/**` and `src/regmap/templates/**` byte-for-byte (`-text`), so
the CRLF reference files and templates are identical on every OS and the byte-level tests pass
on Linux CI as well.
````

- [ ] **Step 3: Final verification**

Run: `pytest -q && ruff check . && ruff format --check . && regmap --version`
Expected: `184 passed`, `All checks passed!`, all files already formatted, `regmap 0.1.0`.

- [ ] **Step 4: Commit**

```bash
git add README.md doc/index.md doc/development.md
git commit -m "docs: README usage, documentation index and development guide"
```
