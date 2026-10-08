# Development

## Setup

```sh
python -m venv .venv
.venv/Scripts/activate        # Windows; on Linux/macOS: source .venv/bin/activate
pip install -e ".[dev]"
```

The `dev` extra installs `pytest`, `ruff` and `openpyxl`.

## Checks

```sh
ruff check .
ruff format --check .
pytest
```

GitHub Actions (`.github/workflows/ci.yml`) runs the same three commands on Python 3.12, 3.13
and 3.14 for every push and pull request.

## Building and publishing

`build.bat` (Windows, run from anywhere) runs the tests, cleans `dist/` and `build/`, and builds
the sdist and wheel into `dist/` with `python -m build`. `build.bat upload` additionally
publishes them to PyPI with `twine`. It needs `build` and `twine` in `.venv`:

```sh
.venv\Scripts\python -m pip install build twine
```

## Project layout

| path | responsibility |
|---|---|
| `src/regmap/codes.py` | Data types, access types, register ID composition, block count. |
| `src/regmap/model.py` | pydantic models of the YAML format, per-object validation, YAML loading. |
| `src/regmap/resolve.py` | Derived values (addresses, IDs, C types, Modbus allocation) and cross-object checks. |
| `src/regmap/templating.py` | Template lookup (project directory first) and placeholder filling. |
| `src/regmap/templates/` | Default C templates shipped with the package. |
| `src/regmap/generators/` | One module per output; they only read the resolved map. `html_doc.py` renders the HTML register table. |
| `src/regmap/outputs.py` | Renders all output files in memory and decides their destinations. |
| `src/regmap/writer.py` | CRLF + UTF-8 writing, unchanged files are not rewritten, `--check` comparison. |
| `src/regmap/check.py` | Stability check against the previous JSON outputs. |
| `src/regmap/cli.py` | `regmap` command line. |
| `src/regmap/scaffold.py` | Starter map for `regmap init`. |
| `src/regmap/importers/` | Excel import (`xlsx.py`) and the readable YAML writer (`yaml_emit.py`). |
| `src/regmap/export_lib.py` | `regmap export-lib`: stamps and writes the firmware library. |
| `src/regmap/firmware_lib/` | Firmware library sources: `core/` (hash-protected) and `port/` (owned by the project). |
| `example/` | VMS-1511 workbook, its VBA outputs (reference) and `vms1511.yaml`. |
| `doc/design/` | Design specs and implementation plans. |

## Tests

- Unit tests per module in `tests/test_<module>.py`; `tests/helpers.py` builds small maps from
  inline YAML.
- `tests/test_reference_vms1511.py` generates `example/vms1511.yaml` and compares with the VBA
  outputs in `example/`: C files byte-for-byte, JSON after the documented differences
  (`doc/differences-from-vba.md`). It also checks that importing `example/Vms1511.xlsm`
  reproduces `example/vms1511.yaml` exactly.
- `tests/test_firmware_core.py` exports the firmware library, generates `example/vms1511.yaml`
  and builds the core with the host `gcc` against the fake port in `tests/c/`, then runs the C
  tests; `tests/test_firmware_ports.py` syntax-checks the STM32 ports against
  `tests/c/hal_stub/`. Both are skipped without `gcc`.

## Starter map for `regmap init`

`src/regmap/templates/init_template.yaml` is imported from `example/Template.xlsm`; edit the
starter registers in that workbook, never the YAML by hand. After a change regenerate it:

```sh
regmap import-xlsx example/Template.xlsm -o src/regmap/templates/init_template.yaml --force
```

`regmap init` (`src/regmap/scaffold.py`) renames the device `Template`, and replaces the
workbook's `generator` section with one listing all outputs as `.`. `tests/test_init.py` fails
when the YAML no longer matches the workbook.

## Line endings

`.gitattributes` stores `example/**` and `src/regmap/templates/**` byte-for-byte (`-text`), so
the CRLF reference files and templates are identical on every OS and the byte-level tests pass
on Linux CI as well.
