# py-reg-map — Register Map Generator: Design

- **Date:** 2026-09-28
- **Status:** Approved in brainstorming, pending written-spec review
- **Repository:** https://github.com/LogicElements/py-reg-map (private)

## 1. Context

Register maps of embedded devices (e.g. VMS-1511) are maintained today in an Excel workbook
(`example/Vms1511.xlsm`). The firmware developer fills the `Registers` sheet by hand; VBA macros
generate C sources for firmware storage, Modbus RTU callbacks, and JSON definitions consumed by
communication software (LeBin protocol and Modbus RTU).

Problems with the current flow: information is repeated on every row (block code, computed
ID, computed name, computed address), the workbook is hard to review in git and hard to edit
with AI tools, and the generator is bound to Excel on Windows.

## 2. Goals

1. A YAML file is the **single source of truth** for a device register map.
2. A Python CLI tool `regmap` generates all outputs from the YAML.
3. Outputs are **content-compatible** with today's VBA outputs: the C API (`CONF_*` macros,
   `conf` storage, `MbRtu_*` callbacks) and the JSON structure/keys stay the same, so firmware
   and communication software need no changes. Obvious VBA bugs are fixed and every
   intentional difference is listed in §9.
4. Existing Excel maps can be migrated with a maintained `regmap import-xlsx` command.
5. The tool protects against accidental changes of register IDs and Modbus addresses.

## 3. Non-goals

- Modbus tag CSV (`mb_rtu_app_tag.csv`), MSSQL scripts, language files and other legacy VBA
  outputs are not generated.
- `IsVisible` and `ConfigUser` keys are dropped from LeBin JSON (and not represented in YAML).
- No integration into firmware builds: the developer runs the tool manually and commits the
  generated files.
- No logger / synced register lists (kept only as empty arrays for firmware API compatibility).

## 4. Decisions (from brainstorming)

| # | Decision |
|---|---|
| 1 | YAML is the source of truth; Excel only for migration. |
| 2 | Outputs content-compatible with VBA outputs; obvious bugs fixed. |
| 3 | Outputs: `reg_map.h/.c`, `mb_rtu_app.h/.c`, `<Name>_registers.json` (LeBin), `<Name>_Modbus.json`, `<Name>Regs.py`. |
| 4 | All outputs UTF-8 (no BOM), CRLF line endings. |
| 5 | CLI installed via `pip` from the git repository, run manually; generated files are committed. |
| 6 | Default templates ship with the tool; a project may override any of them. Placeholders stay as today. |
| 7 | `import-xlsx` is a maintained command of the tool. |
| 8 | ID/Modbus-address stability check against previously generated JSON files. |
| 9 | Architecture: validated model → resolved map → independent generators (approach 1). |
| 10 | Python ≥ 3.12. Code, comments, messages, README in English. |
| 11 | `FACT_SERIAL_NUMBER` stays a special member of the CALIB list. |
| 12 | Output destinations and template directory live in an optional `generator:` section of the YAML. |
| 13 | `README.md` holds only basics (what, install, usage); all behavior and format details live in structured Markdown files in `doc/`. |

## 5. Architecture

### 5.1 Project layout

```
py-reg-map/
  pyproject.toml              # package "py-reg-map", console script "regmap", requires-python >=3.12
  README.md                   # basics only: what it is, install, usage (§14.1)
  doc/                        # structured reference documentation (§14.2)
  .github/workflows/ci.yml    # ruff + pytest on Python 3.12, 3.13, 3.14
  src/regmap/
    __init__.py               # __version__
    cli.py                    # argparse: generate | import-xlsx | schema
    model.py                  # pydantic models: RegisterMap, Device, Generator, Block, Register,
                              #   EnumValue, Bit, ModbusOptions (+ field validation)
    codes.py                  # type/access codes, ID composition, block count (26)
    resolve.py                # RegisterMap -> ResolvedMap (all derived values, cross-checks)
    templates.py              # template lookup (project dir > package default), placeholder fill
    check.py                  # stability check against previous JSON outputs
    writer.py                 # CRLF + UTF-8 writing, skip unchanged files, --check comparison
    generators/
      __init__.py             # common helpers (name width, legend text, number formatting)
      c_regmap.py             # fragments for reg_map.h / reg_map.c
      c_modbus.py             # fragments for mb_rtu_app.h / mb_rtu_app.c
      lebin_json.py
      modbus_json.py
      python_regs.py
    importers/
      xlsx.py                 # Excel -> RegisterMap -> YAML text
      yaml_emit.py            # readable YAML emitter used by the importer
    templates/                # reg_map_temp.h/.c, mb_rtu_app_temp.h/.c (moved from /template)
  tests/
  example/                    # reference inputs/outputs (existing files) + vms1511.yaml
  docs/superpowers/specs/
```

The existing top-level `template/` directory moves to `src/regmap/templates/` unchanged.
`example/` keeps the uploaded reference files and gains `vms1511.yaml` produced by the importer.

### 5.2 Data flow

```
YAML text --(PyYAML safe_load)--> dict --(pydantic)--> RegisterMap
   --(resolve)--> ResolvedMap --(generators)--> {output path: text}
   --(check / writer)--> files
```

- **`model.py`** validates everything that is local to one object (field types, allowed values,
  per-register rules). Unknown keys are errors (`extra="forbid"`).
- **`resolve.py`** computes every derived value exactly once and performs cross-object checks
  (uniqueness, overlaps, Modbus collisions). `ResolvedMap` is an immutable (frozen dataclass)
  structure; generators only read it and never compute addresses or IDs themselves.
- **Generators** are pure functions `ResolvedMap -> str` (JSON, Python) or
  `ResolvedMap -> dict[placeholder, fragment]` (C). They are testable in isolation.
- **Writer** normalizes `\n` to `\r\n`, encodes UTF-8 without BOM, and rewrites a file only if
  its content changed (keeps timestamps, avoids needless firmware rebuilds).

### 5.3 Dependencies

- Runtime: `pyyaml`, `pydantic>=2`.
- Optional extra `xlsx`: `openpyxl` (only for `import-xlsx`).
- Dev: `pytest`, `ruff`.
- Build backend: `hatchling`; templates are package data read via `importlib.resources`.

## 6. YAML format

### 6.1 Example (excerpt of `example/vms1511.yaml`)

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
        description: Binary map of different status flags
        bits:
          - {name: STAT_BIT_ERROR,         bit: 0,   label: Generic error, description: Error in the device}
          - {name: STAT_BIT_CONFIG_FLASH,  bit: 16,  label: Configuration flash error, description: Error when working with configuration memory}

  COM:
    code: 3
    name: COMMUNICATION
    description: Modbus communication parameters
    registers:
      - name: MB_BAUD_RATE
        type: ENUM
        access: RWF
        size: 1
        label: Modbus baud rate
        default: MB_BAUD_19200
        description: Modbus RTU serial port baud rate
        values:
          - {name: MB_BAUD_9600,    label: "9600", description: 9600 baud/s}
          - {name: MB_BAUD_19200,   label: "19200", description: 19200 baud/s}

      - name: MB_ADDRESS
        type: INT
        access: RO
        size: 2
        label: Modbus slave address
        default: 32
        min: 32
        max: 63
        modbus: false
        description: Modbus RTU serial port slave address

  AMP:
    code: 5
    name: AMPLIFIER
    description: Amplifier configuration
    registers:
      - name: GAIN
        type: ENUM
        access: RWF
        size: 1
        label: Amplifier gain
        default: GAIN_1
        modbus: {address: 100}
        values:
          - {name: GAIN_1,   label: Gain 1 x}
          - {name: GAIN_3,   label: Gain 3 x}
```

### 6.2 Top level

| key | required | meaning |
|---|---|---|
| `device.name` | yes | Output file prefix: `<name>_registers.json`, `<name>_Modbus.json`, `<name>Regs.py`, Python class `<name>Regs`. Must be a valid identifier. |
| `device.c_prefix` | no, default `CONF_` | Prefix of register ID macros (`CONF_SYS_UPTIME`) and access macros (`CONF_INT`, `CONF_PTR`, …). |
| `device.c_storage` | no, default `CONF_REG` | Storage naming: `CONF_REG_BLOCK_NUMBER`, `CONF_REG_LIMIT`, `conf_reg_t`, `conf_reg_sys_t`. |
| `generator.templates` | no | Directory with project templates, relative to the YAML file. |
| `generator.outputs.<key>` | no | Destination directory per output, relative to the YAML file. Keys: `reg_map`, `modbus`, `lebin_json`, `modbus_json`, `python`. Default: the YAML file's directory. |
| `blocks` | yes | Mapping `ABBREV -> Block`. |

The storage variable name is always `conf` (as in the current firmware).

### 6.3 Block

| key | required | rules |
|---|---|---|
| (mapping key) | yes | Block abbreviation, `^[A-Z][A-Z0-9_]*$`. |
| `code` | yes | Integer 0–25, unique across blocks. |
| `name` | no | Documentation only (e.g. `SYSTEM`). |
| `description` | no | Documentation only. |
| `registers` | yes | List of registers. An empty list is allowed and treated like an unused code (`NULL` pointer, `res<N>` slot). |

All outputs process blocks in ascending `code` order; the YAML order of blocks is irrelevant.
Registers keep their YAML order within a block.

### 6.4 Register

| key | required | rules |
|---|---|---|
| `name` | yes | `^[A-Za-z_][A-Za-z0-9_]*$`. Full name `<BLOCK>_<name>` must be unique in the map (case-insensitive, because C struct members are lower-cased). |
| `type` | yes | `BIN`, `INT`, `FLOAT`, `STRING`, `ENUM`. |
| `access` | yes | `RO`, `ROF`, `RW`, `RWF`, `RWIF`. |
| `size` | yes | Bytes. `INT`/`BIN`: 1, 2, 4, 8. `FLOAT`: 4. `ENUM`: 1, 2, 4. `STRING`: ≥ 1. |
| `address` | no | Byte offset in block, 0–4095. Default: end of previous register (0 for the first). Must be ≥ end of previous register (gaps allowed, overlaps are errors). `address + size` must be ≤ 4096. |
| `label` | no | Single-line text. |
| `description` | no | Text; may be multi-line (`\|-`). |
| `unit` | no | Text. |
| `default` | no | `INT`/`BIN`: integer, 0 ≤ v < 2^(8·size). `FLOAT`: number. `ENUM`: name of one of its `values`. `STRING`: text, UTF-8 length + 1 ≤ `size`. If `min`/`max` are set, `min ≤ default ≤ max`. |
| `min`, `max` | no | Numbers (hex literals allowed). Not allowed for `ENUM` (derived). `min ≤ max` if both set. |
| `modbus` | no | Omitted: register is exposed on Modbus with automatic address. `false`: not exposed. Mapping `{address: N, format: F32}` (both optional): `address` = fixed start address 0–65535 in the register's space; `format: F32` only for `FLOAT` (IEEE float in 2 registers; default FLOAT encoding is int16 ×10). |
| `values` | `ENUM` only, required, ≥ 1 item | List of `{name, value?, label?, description?}`. `value` defaults to previous value + 1 (first: 0). Names and values unique within the register. |
| `bits` | `BIN` only, optional | List of `{name, bit, label?, description?}`. `bit` required, 0 ≤ bit < 8·size, unique within the register. |

Global rules:

- Bit names and enum value names are unique across the whole map (they become C macros /
  enum constants in one namespace).
- Text fields accept plain numbers (`label: 9600` is read as `"9600"`); YAML booleans in text
  fields (`label: ON`) are rejected with a hint to quote the value.
- Error messages carry the location and register name, e.g.
  `blocks.COM.registers[1] (MB_PARITY).default: 'MB_PARITY_X' is not one of values`.
  All errors are collected and reported together.

`regmap schema` exports the pydantic JSON Schema so VS Code (YAML extension) offers completion
and validation while editing.

## 7. Derived values (`resolve.py`)

- **Full name:** `<BLOCK>_<name>`, e.g. `SYS_UPTIME`; C macro `<c_prefix><full>`.
- **Address:** running byte offset in the block (see §6.4).
- **ID (32 bit):**
  `(code << 24) | (address << 12) | (type_code << 8) | ((1 + 2·access_code) << 4) | floor(log2(size))`
  - type codes: `BIN 0, INT 1, FLOAT 2, STRING 3, ENUM 5`
  - access codes: `RO 0, ROF 1, RW 2, RWF 3, RWIF 3`
- **Block size:** `end = max(address + size)` of its registers (C `LIMIT`), rounded up to a
  multiple of 4 for the `CONF_DIM_CONDITION` check.
- **C member type:** `ENUM` → `<full lower>_t`; `FLOAT` → `float`; size 1/2/4/8 → `uint8_t`,
  `uint16_t`, `uint32_t`, `uint64_t`; any other size → `uint8_t name[size]`.
  Member name = lower-case register `name`. Gaps become `uint8_t reserved<N>[<gap>];` with `N`
  counting from 0 per block.
- **Enum range:** `min = min(values)`, `max = max(values)`.
- **Modbus:**
  - Space: `RO`/`ROF` → `INPUT`, others → `HOLD`.
  - Word count: `INT`/`BIN`/`STRING` → `ceil(size/2)`, `ENUM` → 1, `FLOAT` → 1 (int16 ×10) or
    2 (`F32`).
  - Allocation: registers in output order (block code, then YAML order); each space has a
    counter starting at 0; `modbus.address` sets the counter before allocation; the register
    takes `count` consecutive addresses and the counter continues after them.
  - Every address may be used only once per space (error otherwise).
  - `FIRST` = 0; `LAST` = highest allocated address in the space, or −1 if the space is empty.
- **Flash list:** registers with access `RWF` or `ROF`, in output order.
  `FLASH_LENGTH = Σ(size + 4)`.
- **Calib list:** `<c_prefix>FACT_SERIAL_NUMBER` first (only if register `FACT.SERIAL_NUMBER`
  exists; it contributes 8 bytes), then all `RWIF` registers. `CALIB_LENGTH = 8 + Σ(size + 4)`
  over the `RWIF` registers (without the 8 if the serial number is absent).
- **Logger / synced lists:** always empty; numbers and lengths 0; `LOCAL_LENGTH = 0`.
- **Block count:** constant 26 (`codes.BLOCK_COUNT`).

## 8. Outputs

All outputs are generated in memory first; nothing is written until all checks pass.
Output order everywhere: blocks by `code`, registers by YAML order.

### 8.1 C outputs (templates + placeholders)

Templates (package defaults, overridable by files with the same name in `generator.templates`):

| template | output | placeholders |
|---|---|---|
| `reg_map_temp.h` | `reg_map.h` | `/* < DEFINE REG MAP > */`, `/* < DEFINE REG BITS > */`, `/* < REG MAP PARAMS > */`, `/* < REG MAP TYPEDEFS > */` |
| `reg_map_temp.c` | `reg_map.c` | `/* < DEFINE REG MAP STORAGE > */`, `/* < REG MAP FACTORY > */` |
| `mb_rtu_app_temp.h` | `mb_rtu_app.h` | `/* < MODBUS INPUT DEFINE > */`, `/* < MODBUS HOLD DEFINE > */` |
| `mb_rtu_app_temp.c` | `mb_rtu_app.c` | `/* < READ INPUT REG > */`, `/* < READ HOLD REG > */`, `/* < WRITE HOLD REG > */` |

- A placeholder missing in a (project) template is an error.
- Each fragment consists of lines each terminated by a newline; the fragment replaces the
  placeholder text, the placeholder's own line ending stays (this reproduces the blank lines of
  today's outputs).
- **Normative reference:** for VMS-1511 the generated C files must be byte-identical to
  `example/reg_map.h`, `example/reg_map.c`, `example/mb_rtu_app.h`, `example/mb_rtu_app.c`.
  The rules below describe the formats; where in doubt, the reference files decide.

**Name width `W`** (used for column alignment, reproduces VBA): the maximum length over all
register full names, all bit names and all enum value names in the map.

`reg_map.h`

- `DEFINE REG MAP` — per register:
  `#define <c_prefix><FULL>` + spaces to width (1 + W − len(FULL)) + `0x%08Xu` + `  ///< ` + label
  (trailing space kept when the label is empty).
- `DEFINE REG BITS` — per bit of every BIN register:
  `#define <BIT NAME UPPER>` + `max(30 − len(name), 3)` spaces + `(1 << (<bit>))`.
- `REG MAP PARAMS` — ten `#define <c_storage>_<X>` lines with fixed spacing exactly as in the
  reference (`BLOCK_NUMBER (26)`, `LOGGER_NUMBER`, `CALIB_NUMBER`, `SYNCED_NUMBER`,
  `FLASH_NUMBER`, `LOGGER_LENGTH`, `CALIB_LENGTH`, `SYNCED_LENGTH`, `FLASH_LENGTH`,
  `LOCAL_LENGTH`), an empty line, then
  `#define CONF_DIM_CONDITION ((sizeof(<struct>) != <rounded size>) || … || 0)` over non-empty
  blocks.
- `REG MAP TYPEDEFS` — an empty line, then:
  1. per ENUM register: `typedef enum` / `{` / `  <NAME> = <value>,` … / `}<full lower>_t ;` / empty line
     (the space before `;` is kept);
  2. per non-empty block: `typedef struct __packed __aligned(4)` / `{` / members / `}<struct name>;` / empty line,
     struct name = `lower(<c_storage>_<BLOCK>)_t`;
  3. an empty line, `typedef struct ` (trailing space) / `{` / 26 slots — `  <struct name> <block lower>;`
     for used codes, `  uint32_t res<code+1>;` for unused — / `}` / `lower(<c_storage>)_t;` / empty line.

`reg_map.c`

- `DEFINE REG MAP STORAGE`:
  - `<lower(c_storage)>_t conf;` + two empty lines;
  - `uint8_t* const <S>[<S>_BLOCK_NUMBER] = {` + for each of 26 codes `(uint8_t*)&conf.<block lower>, ` or `NULL, ` + `};` + empty line;
  - `const uint32_t <S>_LIMIT[<S>_BLOCK_NUMBER] = {` / block ends (`0` for unused) joined by `, ` with trailing `, ` / `};` / empty line;
  - `_FLASH`, `_LOGGER`, `_CALIB`, `_SYNCED` arrays in the same shape (`<c_prefix><FULL>, ` items on one line; empty arrays have an empty items line).
- `REG MAP FACTORY` — per register with `default`, two-space indent:
  - `INT`/`BIN`/`ENUM`: macro by size — 1 → `BYTE`, 2 → `SHORT`, otherwise `INT`;
    `<c_prefix><MACRO>(<c_prefix><FULL>)` padded so that ` = ` is aligned (left part width =
    `len("<c_prefix>SHORT(") + len(c_prefix) + W + 1`), then ` = <value>;`. ENUM defaults are
    written as their numeric value, integers in decimal.
  - `FLOAT`: `<c_prefix>FLOAT(...)` aligned the same way, value formatted with 1–6 decimals
    (`2` → `2.0`, `1.25` → `1.25`).
  - `STRING`: `memcpy(<c_prefix>PTR(<c_prefix><FULL>), "<text>", sizeof("<text>"));` (C-escaped).

`mb_rtu_app.h`

- `MODBUS INPUT DEFINE` / `MODBUS HOLD DEFINE`: `#define MB_INPUT_FIRST    0` (resp.
  `MB_HOLD_FIRST     0`), empty line, per allocated word
  `#define MB_<SPACE>_<FULL>[_<j>]` + spaces to width (W + 14 − len(define name)) + `<addr>u`
  (suffix `_<j>` only when the register has more than one word), empty line,
  `#define MB_INPUT_LAST     <n>` (resp. `MB_HOLD_LAST      <n>`).

`mb_rtu_app.c` — per allocated word:

- Read (input and holding):
  ```c
      case MB_<SPACE>_<FULL>[_j]:
        *value = <expr>;
        break;
  ```
  `<expr>`: FLOAT ×10 → `(int16_t)(10 * conf.<blk>.<member>)`; multi-word →
  `*((uint16_t *)<c_prefix>PTR(<c_prefix><FULL>) + <j>)`; otherwise `conf.<blk>.<member>`.
- Write (holding only):
  ```c
      case MB_HOLD_<FULL>[_j]:
        <assignment>
        id = <c_prefix><FULL>;      // only on the last word of the register
        break;
  ```
  `<assignment>`: FLOAT ×10 → `conf.<blk>.<member> = ((float)((int16_t)value)) / 10;`;
  multi-word → `*((uint16_t *)<c_prefix>PTR(<c_prefix><FULL>) + <j>) = value;`;
  ENUM → `conf.<blk>.<member> = (<full lower>_t)value;`; otherwise `conf.<blk>.<member> = value;`.

### 8.2 JSON common rules

- Written by `json.dumps(obj, indent=2, ensure_ascii=False)`, CRLF line endings, final newline.
- Multi-line texts: newlines become `\r\n` inside the JSON string (as today).
- Legend appended to `Description` of `ENUM` registers and of `BIN` registers that have bits:
  - start: `\r\n` only if the register has a non-empty description, then
    `Allowed values: \r\n` (ENUM) or `Meaning of respective bits: \r\n` (BIN);
  - one line per item terminated by `\r\n`: `<prefix><label> - <description>.`, or
    `<prefix><label>.` when the item has no description; label falls back to the item name.
  - `<prefix>` differs per file (see below).

### 8.3 LeBin JSON — `<Name>_registers.json`

A list with one object per register (all registers), keys in this order:

| key | value |
|---|---|
| `Category` | block abbreviation |
| `Name` | register `name` (without block) |
| `Label` | label or `""` |
| `Id` | ID as decimal integer |
| `VarType` | `type` |
| `Access` | `access`, with `RWIF` written as `RWF` |
| `Description` | description (or `""`) + legend; legend item prefix: `""` for ENUM, `[<bit>] - ` for BIN |
| `Value` | default as **string** (`""` if none; ENUM → numeric value as string; numbers via Python `str()`, e.g. `"1001"`, `"1.5"`) |
| `NewValue` | same as `Value` |
| `EnumStr` | ENUM/BIN only (always present for these types, may be empty): item labels |
| `EnumValue` | ENUM/BIN only: enum values / bit numbers |
| `Reset` | same as `Value` |
| `Min` | only if `min` set in YAML (number) |
| `Max` | only if `max` set in YAML (number) |
| `Units` | unit or `""` |

### 8.4 Modbus JSON — `<Name>_Modbus.json`

A list with one object per register exposed on Modbus, keys in this order:

| key | value |
|---|---|
| `Type` | `INPUT` or `HOLD` |
| `Name` | full name |
| `Id` | ID as decimal integer |
| `Address` | list of allocated addresses |
| `Format` | `type`; `FLOAT32` for `format: F32` |
| `Value` | default as **number** (0 if none; ENUM → numeric value; STRING → the text) |
| `Access` | `access` unchanged (including `RWIF`) |
| `Min` | `min` or 0 (ENUM: derived) |
| `Max` | `max` or 0 (ENUM: derived) |
| `Unit` | unit or `""` |
| `Label` | label or `""` |
| `EnumStr`, `EnumValue` | ENUM/BIN only, as in LeBin |
| `Description` | description + legend; item prefix `Value <n> - ` (ENUM) or `Bit <n> - ` (BIN) |

### 8.5 Python — `<Name>Regs.py`

Byte-identical to `example/Vms1511Regs.py` except for the removed BOM:

```python
class Vms1511Regs:
    SYS_UPTIME = "SYS_UPTIME"
    ...


class COM_MB_BAUD_RATE:
    MB_BAUD_9600 = 0
    ...


```

One constant per register (full name), then one class per ENUM register with its values.

## 9. Intentional differences from the VBA outputs

1. LeBin JSON: `IsVisible` and `ConfigUser` removed.
2. Legends: item without description → `<label>.` instead of `<label> - .`; no leading `\r\n`
   when the register has no description; missing label falls back to the item name; no legend
   header for a BIN register without bits.
3. Encoding: UTF-8 without BOM everywhere (LeBin JSON was cp1250; Modbus JSON and `Regs.py`
   had a BOM).
4. JSON whitespace follows `json.dumps(indent=2)` (was hand-concatenated).
5. C enum typedefs honor explicit `value` (VBA numbered 0..n−1 regardless, JSON honored it).
6. Modbus word count is `ceil(size/2)` (VBA used banker's rounding, e.g. 5 bytes → 2 words).
7. C member for sizes other than 1/2/4/8 is always an array (VBA: only >8 or 3).
8. `FACT_SERIAL_NUMBER` is put into the CALIB list only if it exists.
9. Modbus JSON `Value` of a STRING register is a JSON string (VBA produced invalid JSON).
10. `c_prefix` is used everywhere (VBA hard-coded `CONF_` in Modbus code and flash/calib lists).
11. ENUM `Min`/`Max` in Modbus JSON are derived from values (VBA: sheet columns).
12. Factory section alignment uses width `W` of all names (VBA: only names with a factory
    value); hex defaults are printed in decimal.
13. All outputs order blocks by `code` (VBA used sheet order except for C structs).
14. `MB_*_LAST` is the highest allocated address (VBA: counter − 1; equal unless an explicit
    address jumps backwards).

For VMS-1511 only differences 1–4 are observable.

## 10. CLI

```
regmap generate MAP.yaml [--out DIR] [--allow-id-change] [--check]
regmap import-xlsx WORKBOOK.xlsm [-o MAP.yaml] [--force]
regmap schema [-o FILE]
regmap --version
```

### 10.1 `generate`

1. Load + validate + resolve; on errors print all of them and exit 1.
2. Render all outputs in memory (templates from `generator.templates`, falling back to package
   defaults).
3. Destinations: `generator.outputs.<key>` relative to the YAML file, default the YAML file's
   directory; `--out DIR` sends all outputs to `DIR` and ignores `generator.outputs`.
4. `--check`: compare rendered outputs with files on disk; write nothing; list files that would
   change; exit 1 if any differ or are missing, else 0.
5. Otherwise run the stability check (§11); on breaking changes exit 1 without writing.
6. Write changed files only; print `written` / `unchanged` per file.

### 10.2 `schema`

Prints (or writes to `FILE`) the JSON Schema of the YAML format.

### 10.3 Exit codes

`0` success · `1` validation error, breaking change, `--check` difference, import error ·
`2` usage error (bad arguments, input file not found).

## 11. Stability check (`check.py`)

- Runs before writing, against the previous `<Name>_registers.json` and `<Name>_Modbus.json` in
  their destination directories. Previous files are read as UTF-8, falling back to cp1250
  (legacy VBA LeBin files).
- Missing previous files (first generation) → check skipped for that file.
- Registers are matched by `(Category, Name)` in LeBin and by `Name` in Modbus JSON.
- **Breaking changes:** `Id` changed; Modbus `Type` or `Address` changed; register missing
  (removed from the map, or removed from Modbus).
- **Not breaking:** new registers; changes of label, description, unit, default, min/max,
  enum/bit texts.
- On breaking changes: print a table `register | what | old → new`, exit 1, write nothing.
  `--allow-id-change` reports the table as a warning and writes.

## 12. Excel import (`importers/xlsx.py`)

- Requires the `xlsx` extra. Reads the workbook with `openpyxl` twice: cached values
  (`data_only=True`) for data, formulas for destination paths.
- **Registers sheet:** columns are located by header text in the header row (the row containing
  `Block Code`): `Name source`, `Block Code`, `Data Type`, `Access Code`, `Address`,
  `Max length`, `ID [hex]`, `Name`, `Label`, `Factory value`, `Minimal value`,
  `Maximal value`, `Units`, `Modbus special`, `Invalid`, `Description` (header matching ignores
  whitespace/newlines).
- **Common sheet:** block table (`Abbrev.`, `Code`, `Name`, `Description`) located by header text.
  Only blocks that have registers are imported.
- **Row classification** (as VBA): row with `Data Type` and `Name source` → register; following
  rows without `Data Type` but with `Name` → items of the previous ENUM/BIN register (ignored
  with a warning for other types); `stop` and empty rows skipped; rows with `Invalid` set are
  skipped with a warning.
- **Conversion:**
  - `address` written only if it differs from the automatic address;
  - ENUM: default converted to the value name; item `Factory value` → `value:`; sheet min/max
    dropped (warning if they differ from the derived range);
  - BIN: every bit gets an explicit `bit:` (item `Factory value` overrides, otherwise previous + 1,
    starting at 0);
  - `Modbus special`: `x` → `modbus: false`; `HA n` / `IA n` → `{address: n}`; `HR n` / `IR n`
    (relative skip) → absolute `{address: …}` computed with the VBA allocation; `F32` →
    `{format: F32}`; anything else → warning, ignored;
  - hex literals kept as written; descriptions right-trimmed per line; multi-line labels joined
    with a space (warning);
  - `device.name` from the workbook file name; `c_prefix` / `c_storage` from the
    `Config C prefix` / `Config storage` cells, written only if different from defaults;
  - `generator.outputs` from `RegMap destination` → `reg_map`, `Modbus destination` → `modbus`,
    `Tests destination` → `lebin_json`, `modbus_json`, `python`, only when the cell is a formula
    `GetPath()&"\…"` (converted to a path relative to the output YAML); absolute literal paths
    are skipped with a warning.
- **Verification:** the produced YAML is loaded and resolved; the computed ID of every register
  must equal the sheet's `ID [hex]`. Any mismatch → error, exit 1, nothing written.
- **Output:** `-o` defaults to `<workbook stem lower>.yaml` next to the workbook; an existing file
  is not overwritten without `--force`. The YAML is written by `yaml_emit.py` in the style of
  §6.1: header comment, blank line between registers, `values`/`bits` as one-line flow mappings,
  multi-line descriptions as `|-`, scalars quoted only when needed.
- A summary of warnings is printed at the end.

## 13. Testing

- **Unit tests (pytest):**
  - `model`: one failing case per validation rule, asserting the error location/message
    (overlap, unknown key, default out of range, duplicate bit/enum names, `bits` on non-BIN,
    `min` on ENUM, bad STRING default length, `format: F32` on non-FLOAT, …).
  - `resolve`: automatic/explicit addresses and gaps, ID composition, block ends, Modbus
    allocation (`address` jumps, F32, odd sizes, collisions), flash/calib lists.
  - generators: each on small hand-written maps, covering cases absent from VMS-1511 (FLOAT ×10
    and F32, STRING, BIN without bits, explicit enum `value`, gaps, 8-byte INT).
  - `check`: Id change, Modbus move, removed register, `--allow-id-change`, first run, cp1250
    legacy file.
  - `cli`: exit codes, `--out`, `--check`, unchanged files not rewritten.
- **Reference test (VMS-1511):** generate `example/vms1511.yaml` into a temp directory, then
  - `reg_map.h/.c`, `mb_rtu_app.h/.c`: byte-identical to `example/`;
  - `Vms1511Regs.py`: byte-identical after stripping the BOM from the reference;
  - both JSON files: parsed and compared as objects after applying the documented differences
    (§9 items 1–2) to the reference; nothing else may differ.
- **Import test:** `import-xlsx example/Vms1511.xlsm` must reproduce the committed
  `example/vms1511.yaml` byte-for-byte.
- **CI:** GitHub Actions runs `ruff check`, `ruff format --check` and `pytest` on Python 3.12,
  3.13 and 3.14.
- **Line endings in git:** `.gitattributes` marks `example/**` and `src/regmap/templates/**` as
  `-text` (stored and checked out byte-for-byte, CRLF preserved), so the byte-identical tests
  pass on Linux CI and on Windows regardless of `core.autocrlf`. Templates are read with
  newline normalization anyway, so a project template with LF endings also works.
- If the July 2024 reference outputs turn out to differ from the October 2024 workbook, the
  reference test shows it; each such case is resolved explicitly (documented difference or
  regenerated reference).

## 14. Documentation

### 14.1 `README.md` — basics only

- What the package is (2–3 sentences) and which outputs it produces.
- Installation: `pip install "py-reg-map[xlsx] @ git+https://github.com/LogicElements/py-reg-map"`
  (the `xlsx` extra only for `import-xlsx`).
- Usage: `regmap import-xlsx`, `regmap generate` (incl. `--check`, `--allow-id-change`),
  `regmap schema` — one short example each.
- Link to `doc/index.md` for everything else.

### 14.2 `doc/` — structured reference

Generator behavior, formats and all other details live in `doc/` (English, Markdown):

| file | content |
|---|---|
| `doc/index.md` | Overview, data flow (YAML → model → resolved map → outputs), table of contents. |
| `doc/yaml-format.md` | Full YAML reference: top level, `device`, `generator`, blocks, registers, enum values, bits, `modbus`; validation rules; example. VS Code schema setup (`regmap schema -o regmap.schema.json` + `# yaml-language-server: $schema=regmap.schema.json` first line; the importer does not add this line). |
| `doc/derived-values.md` | Full names, addresses, ID composition, C member types, block sizes, enum ranges, Modbus allocation, flash/calib lists. |
| `doc/outputs.md` | Every output file: C placeholders and fragment formats, LeBin JSON keys, Modbus JSON keys, Python class; encoding and line endings. |
| `doc/templates.md` | Default templates, project override via `generator.templates`, placeholder list, rules for custom templates. |
| `doc/cli.md` | Commands, options, destinations (`generator.outputs`, `--out`), exit codes. |
| `doc/stability-check.md` | What counts as a breaking change, how the check reads previous outputs, `--allow-id-change`. |
| `doc/import-xlsx.md` | Excel migration: expected sheet layout, row classification, value conversion, warnings, ID verification. |
| `doc/differences-from-vba.md` | The intentional differences from the VBA outputs (§9). |
| `doc/development.md` | Dev setup, project layout, tests (unit, reference, import), CI, `.gitattributes` line-ending rule. |

The docs are written together with the code they describe (each implementation task updates
its doc page), not as a final step.
