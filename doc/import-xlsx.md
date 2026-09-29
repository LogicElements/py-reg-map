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
