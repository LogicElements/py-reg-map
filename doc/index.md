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
| [firmware-lib.md](firmware-lib.md) | Firmware library from `regmap export-lib`: LeBin, Modbus RTU slave, upgrade, integration. |
| [firmware-porting.md](firmware-porting.md) | Port contract and porting to other MCU families. |
| [stability-check.md](stability-check.md) | Protection against changed register IDs and Modbus addresses. |
| [import-xlsx.md](import-xlsx.md) | Migrating an Excel/VBA register map. |
| [differences-from-vba.md](differences-from-vba.md) | Intentional differences from the former VBA outputs. |
| [development.md](development.md) | Development setup, layout, tests, CI. |
| [design/](design/) | Design specs and implementation plans. |
