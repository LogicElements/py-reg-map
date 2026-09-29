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
