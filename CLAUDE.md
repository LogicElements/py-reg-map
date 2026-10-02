# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

`regmap` (PyPI: `py-reg-map`) is a register-map generator for embedded firmware. A YAML map is the
single source of truth; it generates C storage + Modbus RTU sources (`reg_map.h/.c`,
`mb_rtu_app.h/.c`), LeBin/Modbus JSON definitions and a Python registers file. It replaces an older
Excel/VBA macro, and the VBA outputs in `example/` are the byte-level reference. User docs live in
`doc/` (start at `doc/index.md`); design specs/plans in `doc/design/`.

## Commands

```sh
pip install -e ".[dev]"          # pytest, ruff, openpyxl (xlsx import is an optional extra)
ruff check .
ruff format --check .
pytest                           # CI runs these three on Python 3.12–3.14
pytest tests/test_resolve.py::test_name -q   # single test
build.bat                        # Windows: tests + sdist/wheel into dist/; `build.bat upload` publishes to PyPI
```

Line length is 100. `example/` and `doc/` are excluded from ruff.

## Architecture

Pipeline: **YAML → `model.py` (pydantic, per-object validation) → `resolve.py` (derived values +
cross-object checks) → `outputs.py` (render everything in memory) → `writer.py`**.

- `codes.py`: data/access types, register ID composition, block counts.
- `resolve.py`: computes addresses, IDs, C types and Modbus allocation. Generators only read this
  resolved map and never compute derived values themselves.
- `generators/`: one module per output. C outputs are filled from templates (`templating.py`,
  default templates in `src/regmap/templates/`; a template in the project directory overrides them).
- `outputs.py` decides destinations; `writer.py` writes CRLF + UTF-8, skips unchanged files, and
  backs `generate --check`.
- `check.py`: **stability check**. Before writing, new LeBin/Modbus JSON is compared with the
  previously generated JSON; changed IDs/Modbus addresses or removed registers abort with exit 1
  unless `--allow-id-change` is passed.
- `importers/`: `xlsx.py` (Excel → YAML migration) and `yaml_emit.py` (readable YAML writer).
- `cli.py`: `regmap generate | import-xlsx | schema`. Errors are reported as `error: ...` on stderr;
  exit 1 for map/template/breaking-change problems, 2 for missing file.

## Testing and byte-exact files

- `tests/test_reference_vms1511.py` generates `example/vms1511.yaml` and compares with the VBA outputs
  in `example/`: C files byte-for-byte, JSON after the differences documented in
  `doc/differences-from-vba.md`. It also asserts that importing `example/Vms1511.xlsm` reproduces
  `example/vms1511.yaml` exactly. Changing generator output or the importer usually requires
  updating these references deliberately.
- `tests/helpers.py` builds small maps from inline YAML for unit tests.
- `.gitattributes` marks `example/**` and `src/regmap/templates/**` as `-text` (CRLF kept
  byte-for-byte); don't let editors or git normalize their line endings.

## Conventions

- Commit messages use conventional prefixes (`feat:`, `fix:`, `docs:`, `test:`, `chore:`).
- Repo content (code, docs, commits) is in English.
