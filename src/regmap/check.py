"""Stability check: compare IDs and Modbus addresses with the previously generated JSON files."""

import json
from dataclasses import dataclass
from typing import Any

from regmap.outputs import OutputFile
from regmap.templating import read_text_file


@dataclass(frozen=True)
class Change:
    register: str
    what: str
    old: str
    new: str


class PreviousOutputError(Exception):
    """A previous output file exists but cannot be read as a JSON list."""


def _previous(output: OutputFile) -> list[dict[str, Any]] | None:
    if not output.path.is_file():
        return None
    try:
        data = json.loads(read_text_file(output.path))
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        raise PreviousOutputError(f"{output.path}: cannot read previous file: {exc}") from None
    if not isinstance(data, list):
        raise PreviousOutputError(f"{output.path}: previous file is not a JSON list")
    return [e for e in data if isinstance(e, dict)]


def _hex(value: Any) -> str:
    return f"0x{value:08X}" if isinstance(value, int) else str(value)


def _placement(entry: dict[str, Any]) -> str:
    return f"{entry.get('Type')} {entry.get('Address')}"


def compare_lebin(old: list[dict[str, Any]], new: list[dict[str, Any]]) -> list[Change]:
    current = {(e.get("Category"), e.get("Name")): e for e in new}
    changes = []
    for e in old:
        key = (e.get("Category"), e.get("Name"))
        if None in key or "Id" not in e:
            continue
        name = f"{key[0]}_{key[1]}"
        if key not in current:
            changes.append(Change(name, "register", "present", "removed"))
        elif current[key].get("Id") != e["Id"]:
            changes.append(Change(name, "Id", _hex(e["Id"]), _hex(current[key].get("Id"))))
    return changes


def compare_modbus(old: list[dict[str, Any]], new: list[dict[str, Any]]) -> list[Change]:
    current = {e.get("Name"): e for e in new}
    changes = []
    for e in old:
        name = e.get("Name")
        if name is None:
            continue
        if name not in current:
            changes.append(Change(name, "Modbus", _placement(e), "not exposed"))
        elif _placement(current[name]) != _placement(e):
            changes.append(Change(name, "Modbus", _placement(e), _placement(current[name])))
    return changes


def breaking_changes(outputs: list[OutputFile]) -> list[Change]:
    """Breaking changes against the files currently on disk (empty on the first generation)."""
    changes = []
    for output in outputs:
        compare = {"lebin_json": compare_lebin, "modbus_json": compare_modbus}.get(output.key)
        if compare is None:
            continue
        old = _previous(output)
        if old is not None:
            changes += compare(old, json.loads(output.text))
    return changes


def format_changes(changes: list[Change]) -> str:
    width = max(len(c.register) for c in changes)
    lines = [f"  {c.register.ljust(width)}  {c.what}: {c.old} -> {c.new}" for c in changes]
    return "\n".join(lines)
