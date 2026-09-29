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
