"""Syntax check of the STM32 ports against a minimal HAL stub (no target toolchain on CI)."""

import shutil
import subprocess
from pathlib import Path

import pytest
from test_firmware_core import generate_vms1511

from regmap.export_lib import export, library_files

HAL_STUB = Path(__file__).parent / "c" / "hal_stub"
FLAGS = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-fshort-enums", "-fsyntax-only"]
VARIANTS = {  # file -> list of extra -D option sets to check
    "port_stm32.c": [[], ["-DLEBIN_PORT_TRANSPORT=2"], ["-DREGMAP_LIB_HAL_UART_CALLBACKS=0"]],
    "modbus_port_stm32.c": [
        [],
        ["-DMB_PORT_USE_DMA=1"],
        ["-DMB_PORT_USE_DMA=1", "-D__DCACHE_PRESENT=1U"],
        ["-DMB_PORT_HW_DE=1"],
    ],
    "lebin_port_usb_cdc.c": [[]],
    "lebin_port_uart.c": [["-DLEBIN_PORT_TRANSPORT=2"]],
    "upgrade_port_stm32.c": [[], ["-DSTUB_FAMILY_PAGE"]],
}

pytestmark = pytest.mark.skipif(shutil.which("gcc") is None, reason="gcc not available")


@pytest.fixture(scope="module")
def tree(tmp_path_factory) -> tuple[Path, Path]:
    tmp = tmp_path_factory.mktemp("ports")
    export(tmp / "lib")
    generate_vms1511(tmp / "gen")
    return tmp / "lib", tmp / "gen"


def test_every_stm32_port_file_is_checked():
    ports = {f.name for f in library_files() if not f.core and f.name.endswith(".c")}
    assert ports - {"config_app.c"} == set(VARIANTS)


@pytest.mark.parametrize(
    ("name", "defines"), [(n, d) for n, sets in VARIANTS.items() for d in sets]
)
def test_port_compiles_against_hal_stub(tree, name, defines):
    lib, gen = tree
    cmd = ["gcc", *FLAGS, *defines, f"-I{HAL_STUB}", f"-I{lib}", f"-I{gen}", str(lib / name)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    assert result.returncode == 0, f"{' '.join(cmd)}\n{result.stderr}"
