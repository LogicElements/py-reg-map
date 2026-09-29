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
