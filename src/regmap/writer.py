"""Write output files: UTF-8 without BOM, CRLF line endings, unchanged files left alone."""

from pathlib import Path

from regmap.outputs import OutputFile


def encode(text: str) -> bytes:
    return text.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8")


def is_current(output: OutputFile) -> bool:
    return output.path.is_file() and output.path.read_bytes() == encode(output.text)


def stale(outputs: list[OutputFile]) -> list[Path]:
    """Files that are missing or differ from what would be written (for --check)."""
    return [o.path for o in outputs if not is_current(o)]


def write(outputs: list[OutputFile]) -> list[tuple[Path, str]]:
    """Write changed files; returns (path, "written" | "unchanged") per file."""
    report = []
    for o in outputs:
        if is_current(o):
            report.append((o.path, "unchanged"))
            continue
        o.path.parent.mkdir(parents=True, exist_ok=True)
        o.path.write_bytes(encode(o.text))
        report.append((o.path, "written"))
    return report
