"""regmap export-lib: copy the firmware communication library into a firmware project.

Core files belong to regmap: re-export updates them unless they were edited by hand, which is
detected by the content hash stamped into their ``@regmap-lib`` line. Port files belong to the
project: they are written once and kept afterwards.
"""

import hashlib
import re
from dataclasses import dataclass
from importlib.resources import files
from pathlib import Path

from regmap import __version__
from regmap.templating import read_text_file
from regmap.writer import encode

MARKER = "@regmap-lib"
_STAMP = re.compile(rf"{MARKER} \S+ sha256:([0-9a-f]{{16}})")


class ExportError(Exception):
    def __init__(self, paths: list[Path]):
        super().__init__(f"{len(paths)} library file(s) were edited by hand")
        self.paths = paths


@dataclass(frozen=True)
class LibFile:
    name: str
    core: bool
    text: str  # "\n" line endings; core files already stamped


def _lines_without_marker(text: str) -> str:
    lines = text.replace("\r\n", "\n").split("\n")
    return "\n".join(line for line in lines if MARKER not in line)


def content_hash(text: str) -> str:
    """Hash of the file content without its marker line (line endings normalised)."""
    return hashlib.sha256(_lines_without_marker(text).encode("utf-8")).hexdigest()[:16]


def stamp(text: str) -> str:
    """Complete the marker line with the regmap version and the content hash."""
    lines = text.replace("\r\n", "\n").split("\n")
    index = next((i for i, line in enumerate(lines) if MARKER in line), None)
    if index is None:
        raise ValueError(f"no {MARKER} line")
    prefix = lines[index][: lines[index].index(MARKER)]
    lines[index] = f"{prefix}{MARKER} {__version__} sha256:{content_hash(text)}"
    return "\n".join(lines)


def is_pristine(text: str) -> bool:
    """True when the stamped hash matches the content, i.e. nobody edited the file."""
    match = _STAMP.search(text)
    return match is not None and match.group(1) == content_hash(text)


def library_files() -> list[LibFile]:
    result = []
    for kind in ("core", "port"):
        folder = files("regmap").joinpath("firmware_lib", kind)
        for entry in sorted(folder.iterdir(), key=lambda e: e.name):
            if not entry.name.endswith((".c", ".h")):
                continue
            text = entry.read_text(encoding="utf-8").replace("\r\n", "\n")
            core = kind == "core"
            result.append(LibFile(entry.name, core, stamp(text) if core else text))
    return result


def export(target: Path, force: bool = False, force_ports: bool = False) -> list[tuple[Path, str]]:
    """Write the library into ``target``; returns (path, status) per file."""
    lib = library_files()
    if not force:
        edited = [
            target / f.name
            for f in lib
            if f.core
            and (target / f.name).is_file()
            and not is_pristine(read_text_file(target / f.name))
        ]
        if edited:
            raise ExportError(edited)

    report = []
    for f in lib:
        path = target / f.name
        data = encode(f.text)
        if path.is_file():
            if path.read_bytes() == data:
                report.append((path, "unchanged"))
                continue
            if not f.core and not force_ports:
                report.append((path, "kept"))
                continue
            status = "updated"
        else:
            status = "written"
        target.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        report.append((path, status))
    return report
