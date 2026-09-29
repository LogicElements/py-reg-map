"""C templates: lookup (project directory first, then package defaults) and placeholder filling."""

from importlib.resources import files
from pathlib import Path


class TemplateError(Exception):
    pass


def read_text_file(path: Path) -> str:
    """Read UTF-8 (with or without BOM), falling back to cp1250 for legacy files."""
    data = path.read_bytes()
    try:
        return data.decode("utf-8-sig")
    except UnicodeDecodeError:
        return data.decode("cp1250")


def load_template(name: str, project_dir: Path | None = None) -> str:
    """Template text with ``\\n`` line endings and a final newline."""
    if project_dir is not None and (project_dir / name).is_file():
        text = read_text_file(project_dir / name)
    else:
        text = files("regmap").joinpath("templates", name).read_text(encoding="utf-8")
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    return text if text.endswith("\n") else text + "\n"


def fill(template: str, fragments: dict[str, str], name: str) -> str:
    """Replace every placeholder by its fragment; a missing placeholder is an error."""
    missing = [p for p in fragments if p not in template]
    if missing:
        raise TemplateError(f"{name}: placeholder {missing[0]} not found in template")
    for placeholder, text in fragments.items():
        template = template.replace(placeholder, text)
    return template
