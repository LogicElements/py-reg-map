"""Build the exported core library with the host C compiler and run the C unit tests."""

import os
import shutil
import subprocess
from pathlib import Path

import pytest
from helpers import EXAMPLE

from regmap.export_lib import export, library_files
from regmap.model import load_map
from regmap.outputs import render_outputs
from regmap.resolve import resolve
from regmap.writer import write

C_TESTS = Path(__file__).parent / "c"
HOST_PORT_FILES = ["config_app.c"]  # port sources without HAL; STM32 ports cannot build on a PC
FLAGS = ["-std=c99", "-Wall", "-Wextra", "-fshort-enums"]
if os.name == "nt":
    FLAGS.append("-mno-ms-bitfields")  # MinGW ignores __packed otherwise; target GCC never does

pytestmark = pytest.mark.skipif(shutil.which("gcc") is None, reason="gcc not available")


def run(cmd: list[str]) -> None:
    result = subprocess.run(cmd, capture_output=True, text=True)
    assert result.returncode == 0, f"{' '.join(cmd)}\n{result.stdout}{result.stderr}"


def generate_vms1511(out: Path) -> None:
    rmap = load_map(EXAMPLE / "vms1511.yaml")
    outputs = render_outputs(resolve(rmap), rmap.generator, EXAMPLE, out)
    write([o for o in outputs if o.key in ("reg_map", "modbus")])


@pytest.fixture(scope="module")
def core_binary(tmp_path_factory) -> Path:
    tmp = tmp_path_factory.mktemp("fw")
    lib, gen, obj = tmp / "lib", tmp / "gen", tmp / "obj"
    export(lib)
    generate_vms1511(gen)
    obj.mkdir()
    includes = [f"-I{p}" for p in (C_TESTS, lib, gen)]
    sources = [(lib / f.name, True) for f in library_files() if f.core and f.name.endswith(".c")]
    sources += [(lib / name, True) for name in HOST_PORT_FILES]
    sources += [(gen / "reg_map.c", False), (gen / "mb_rtu_app.c", False)]
    sources += [(path, False) for path in sorted(C_TESTS.glob("*.c"))]
    objects = []
    for source, strict in sources:
        target = obj / f"{source.stem}.o"
        run(
            [
                "gcc",
                *FLAGS,
                *(["-Werror"] if strict else []),
                *includes,
                "-c",
                str(source),
                "-o",
                str(target),
            ]
        )
        objects.append(str(target))
    binary = tmp / ("core_tests.exe" if os.name == "nt" else "core_tests")
    run(["gcc", *objects, "-o", str(binary)])
    return binary


def test_core_library(core_binary):
    result = subprocess.run([str(core_binary)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "all checks passed" in result.stdout
