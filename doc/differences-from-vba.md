# Differences from the VBA outputs

The outputs keep the C API and the JSON structure of the former Excel/VBA generator, so
firmware and communication software need no changes. These differences are intentional:

1. LeBin JSON: the keys `IsVisible` (always `true`) and `ConfigUser` (set by accident from
   the `Modbus special` column) are removed.
2. Legends in descriptions: an item without description is written as `<label>.` instead of
   `<label> - .`; the legend does not start with an empty line when the register has no
   description; an item without label uses its name; a BIN register without bits gets no
   legend header.
3. Encoding: all files are UTF-8 without BOM (the LeBin JSON was cp1250, the Modbus JSON and
   the Python file had a BOM).
4. JSON whitespace follows `json.dumps(indent=2)` instead of hand-made formatting.
5. C enum typedefs use explicit enum values (VBA numbered 0..n−1 in C but honored the values
   in JSON).
6. Modbus word count is `ceil(size / 2)` (VBA used banker's rounding, 5 bytes → 2 words).
7. C members of sizes other than 1/2/4/8 bytes are always arrays (VBA: only above 8 or 3).
8. `FACT_SERIAL_NUMBER` enters the calibration list only when the map has it.
9. Modbus JSON `Value` of a STRING register is a JSON string (VBA wrote invalid JSON).
10. `c_prefix` is used everywhere (VBA hard-coded `CONF_` in the Modbus code and the
    flash/calibration lists).
11. ENUM `Min`/`Max` in Modbus JSON come from the values (VBA: sheet columns).
12. Factory values are aligned by the width of all names (VBA: names with a factory value
    only); hex defaults are printed in decimal.
13. All outputs order blocks by `code` (VBA used sheet order except for C structures).
14. `MB_*_LAST` is the highest allocated address (VBA: counter − 1; equal unless an explicit
    address jumps backwards).
15. Descriptions lose trailing spaces at line ends (the importer removes them).

For VMS-1511 only differences 1–4 and 15 are visible; `tests/test_reference_vms1511.py`
applies exactly these to the VBA files and requires everything else to match.
