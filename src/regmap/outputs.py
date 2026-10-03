"""Render all output files of a map in memory and decide where they go."""

from dataclasses import dataclass
from pathlib import Path

from regmap.generators import c_modbus, c_regmap, html_doc, lebin_json, modbus_json, python_regs
from regmap.model import GeneratorSettings
from regmap.resolve import ResolvedMap
from regmap.templating import TemplateError, fill, load_template

OUTPUT_KEYS = ("reg_map", "modbus", "lebin_json", "modbus_json", "python", "html")


@dataclass(frozen=True)
class OutputFile:
    key: str  # one of OUTPUT_KEYS
    path: Path
    text: str  # "\n" line endings; the writer converts to CRLF


def destination(
    key: str, settings: GeneratorSettings, base_dir: Path, out_dir: Path | None
) -> Path:
    if out_dir is not None:
        return out_dir
    configured = getattr(settings.outputs, key)
    return base_dir / configured if configured else base_dir


def render_outputs(
    rmap: ResolvedMap, settings: GeneratorSettings, base_dir: Path, out_dir: Path | None = None
) -> list[OutputFile]:
    """All output files; ``base_dir`` is the YAML file's directory."""
    project = base_dir / settings.templates if settings.templates else None
    if project is not None and not project.is_dir():
        raise TemplateError(f"generator.templates: templates directory {project} does not exist")

    def c_file(key: str, template: str, output: str, fragments: dict[str, str]) -> OutputFile:
        text = fill(load_template(template, project), fragments, template)
        return OutputFile(key, destination(key, settings, base_dir, out_dir) / output, text)

    def data_file(key: str, filename: str, text: str) -> OutputFile:
        return OutputFile(key, destination(key, settings, base_dir, out_dir) / filename, text)

    files = [
        c_file("reg_map", "reg_map_temp.h", "reg_map.h", c_regmap.header_fragments(rmap)),
        c_file("reg_map", "reg_map_temp.c", "reg_map.c", c_regmap.source_fragments(rmap)),
        c_file("modbus", "mb_rtu_app_temp.h", "mb_rtu_app.h", c_modbus.header_fragments(rmap)),
        c_file("modbus", "mb_rtu_app_temp.c", "mb_rtu_app.c", c_modbus.source_fragments(rmap)),
        data_file("lebin_json", f"{rmap.name}_registers.json", lebin_json.render(rmap)),
        data_file("modbus_json", f"{rmap.name}_Modbus.json", modbus_json.render(rmap)),
        data_file("python", f"{rmap.name}Regs.py", python_regs.render(rmap)),
    ]
    if settings.outputs.html is not False:
        files.append(data_file("html", f"{rmap.name}_registers.html", html_doc.render(rmap)))
    return files
