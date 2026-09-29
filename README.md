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
