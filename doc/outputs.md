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
