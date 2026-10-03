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
| `outputs.html` | Directory for `<name>_registers.html`; `false` does not write it. |

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

`regmap schema -o regmap.schema.json` writes a JSON Schema of this format; `regmap init` writes
it next to every new map.

- **VS Code** (Red Hat YAML extension): the first line of the map selects the schema, relative
  to the map file. `regmap init` adds it; for other maps add it by hand:

  ```yaml
  # yaml-language-server: $schema=regmap.schema.json
  ```

- **PyCharm** ignores that comment. Add the schema in Settings → Languages & Frameworks →
  Schemas and DNS → JSON Schema: choose `regmap.schema.json`, schema version 2020-12 or 2019-09,
  and map it to the map file (or a file pattern such as `*.yaml`).

Regenerate the schema after upgrading `regmap`. The importer does not add the comment.
