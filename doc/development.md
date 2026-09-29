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
| `src/regmap/generators/` | One module per output; they only read the resolved map. |
| `src/regmap/outputs.py` | Renders all output files in memory and decides their destinations. |
| `src/regmap/writer.py` | CRLF + UTF-8 writing, unchanged files are not rewritten, `--check` comparison. |
| `src/regmap/check.py` | Stability check against the previous JSON outputs. |
| `src/regmap/cli.py` | `regmap` command line. |
| `src/regmap/importers/` | Excel import (`xlsx.py`) and the readable YAML writer (`yaml_emit.py`). |
| `example/` | VMS-1511 workbook, its VBA outputs (reference) and `vms1511.yaml`. |
| `doc/design/` | Design specs and implementation plans. |

## Tests

- Unit tests per module in `tests/test_<module>.py`; `tests/helpers.py` builds small maps from
  inline YAML.
- `tests/test_reference_vms1511.py` generates `example/vms1511.yaml` and compares with the VBA
  outputs in `example/`: C files byte-for-byte, JSON after the documented differences
  (`doc/differences-from-vba.md`). It also checks that importing `example/Vms1511.xlsm`
  reproduces `example/vms1511.yaml` exactly.

## Line endings

`.gitattributes` stores `example/**` and `src/regmap/templates/**` byte-for-byte (`-text`), so
the CRLF reference files and templates are identical on every OS and the byte-level tests pass
on Linux CI as well.
