"""Command line interface: regmap generate | import-xlsx | schema."""

import argparse
import json
import sys
from collections.abc import Sequence
from pathlib import Path

from regmap import __version__
from regmap.check import PreviousOutputError, breaking_changes, format_changes
from regmap.model import MapError, RegisterMap, load_map
from regmap.outputs import render_outputs
from regmap.resolve import resolve
from regmap.templating import TemplateError
from regmap.writer import encode, stale, write


def _error(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)


def _generate(args: argparse.Namespace) -> int:
    path: Path = args.map
    if not path.is_file():
        _error(f"{path}: file not found")
        return 2
    try:
        rmap = load_map(path)
        resolved = resolve(rmap)
        outputs = render_outputs(resolved, rmap.generator, path.parent, args.out)
    except MapError as exc:
        _error(f"{path}: {len(exc.errors)} problem(s)")
        for message in exc.errors:
            print(f"  {message}", file=sys.stderr)
        return 1
    except TemplateError as exc:
        _error(str(exc))
        return 1

    if args.check:
        outdated = stale(outputs)
        for p in outdated:
            print(f"outdated  {p}")
        return 1 if outdated else 0

    try:
        changes = breaking_changes(outputs)
    except PreviousOutputError as exc:
        if not args.allow_id_change:
            _error(f"{exc}; use --allow-id-change to overwrite it")
            return 1
        changes = []
    if changes:
        table = format_changes(changes)
        if not args.allow_id_change:
            _error(f"breaking changes against the previous outputs, nothing written:\n{table}")
            print("use --allow-id-change if the change is intended", file=sys.stderr)
            return 1
        print(f"warning: breaking changes accepted (--allow-id-change):\n{table}", file=sys.stderr)

    for p, status in write(outputs):
        print(f"{status:9} {p}")
    return 0


def _schema(args: argparse.Namespace) -> int:
    text = json.dumps(RegisterMap.model_json_schema(), indent=2) + "\n"
    if args.output is None:
        sys.stdout.write(text)
    else:
        args.output.write_bytes(encode(text))
    return 0


def _import_xlsx(args: argparse.Namespace) -> int:
    try:
        from regmap.importers.xlsx import ImportFailed, import_workbook
    except ImportError:
        _error('import-xlsx needs openpyxl: pip install "py-reg-map[xlsx]"')
        return 1
    workbook: Path = args.workbook
    if not workbook.is_file():
        _error(f"{workbook}: file not found")
        return 2
    target: Path = args.output or workbook.with_name(workbook.stem.lower() + ".yaml")
    if target.exists() and not args.force:
        _error(f"{target} already exists; use --force to overwrite it")
        return 1
    try:
        result = import_workbook(workbook, target)
    except ImportFailed as exc:
        _error(f"{workbook}: import failed")
        for message in exc.errors:
            print(f"  {message}", file=sys.stderr)
        return 1
    target.write_bytes(encode(result.yaml_text))
    for warning in result.warnings:
        print(f"warning: {warning}", file=sys.stderr)
    print(
        f"written   {target} ({result.register_count} registers, {len(result.warnings)} warnings)"
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="regmap", description="Register map generator")
    parser.add_argument("--version", action="version", version=f"regmap {__version__}")
    sub = parser.add_subparsers(dest="command", required=True)

    gen = sub.add_parser("generate", help="generate C and JSON outputs from a YAML map")
    gen.add_argument("map", type=Path, help="register map YAML file")
    gen.add_argument("--out", type=Path, help="write all outputs to this directory")
    gen.add_argument("--allow-id-change", action="store_true", help="accept breaking changes")
    gen.add_argument("--check", action="store_true", help="only check that outputs are current")
    gen.set_defaults(func=_generate)

    imp = sub.add_parser("import-xlsx", help="convert a legacy Excel register map to YAML")
    imp.add_argument("workbook", type=Path, help="Excel workbook (.xlsm/.xlsx)")
    imp.add_argument("-o", "--output", type=Path, help="YAML file to write")
    imp.add_argument("--force", action="store_true", help="overwrite an existing YAML file")
    imp.set_defaults(func=_import_xlsx)

    sch = sub.add_parser("schema", help="print the JSON Schema of the YAML format")
    sch.add_argument("-o", "--output", type=Path, help="write the schema to this file")
    sch.set_defaults(func=_schema)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except (OSError, UnicodeDecodeError) as exc:
        _error(str(exc))
        return 1
