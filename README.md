# py-reg-map

[![PyPI](https://img.shields.io/pypi/v/py-reg-map)](https://pypi.org/project/py-reg-map/)
[![Python](https://img.shields.io/pypi/pyversions/py-reg-map)](https://pypi.org/project/py-reg-map/)
[![CI](https://github.com/LogicElements/py-reg-map/actions/workflows/ci.yml/badge.svg)](https://github.com/LogicElements/py-reg-map/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/pypi/l/py-reg-map)](LICENSE)

Register map generator for embedded firmware. A YAML file describes the device registers;
`regmap` generates the C storage and Modbus RTU sources for the firmware (`reg_map.h/.c`,
`mb_rtu_app.h/.c`) and the JSON definitions for the LeBin and Modbus RTU communication
software.

## Installation

Python 3.12 or newer. Install from [PyPI](https://pypi.org/project/py-reg-map/):

```sh
pip install py-reg-map
pip install "py-reg-map[xlsx]"   # with Excel import
```

The latest development version comes straight from GitHub:

```sh
pip install "py-reg-map @ git+https://github.com/LogicElements/py-reg-map"
```

## Usage

Start a new register map:

```sh
regmap init MyDevice                              # -> mydevice.yaml
```

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

Add the firmware communication library (LeBin, Modbus RTU slave) to the firmware project:

```sh
regmap export-lib Core/RegMap
```

Editor completion for the YAML format:

```sh
regmap schema -o Documents/regmap.schema.json
```

## Documentation

Start at [doc/index.md](doc/index.md): [YAML format](doc/yaml-format.md),
[command line](doc/cli.md), [outputs](doc/outputs.md), [firmware library](doc/firmware-lib.md) and
[development guide](doc/development.md).

## License

[MIT](LICENSE) © 2026 Logic Elements s.r.o
