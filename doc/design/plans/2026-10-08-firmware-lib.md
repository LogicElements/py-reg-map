# Firmware Communication Library (`regmap export-lib`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `regmap export-lib <dir>` writes a portable C library (LeBin, Modbus RTU slave, firmware upgrade, register access) plus editable STM32 HAL ports into a firmware project.

**Architecture:** Library sources are package data in `src/regmap/firmware_lib/core` (plain C99, no HAL, updated on re-export, protected by a content hash) and `src/regmap/firmware_lib/port` (STM32 HAL glue and configuration, written once). `src/regmap/export_lib.py` stamps and writes them; `cli.py` exposes the command. The core is tested on the host: pytest exports the library, generates `example/vms1511.yaml`, compiles everything with `gcc` against a fake port and runs a C test binary.

**Tech Stack:** Python 3.12 (argparse, hashlib, importlib.resources, pytest), C99 (gcc on host; arm-none-eabi-gcc + STM32Cube HAL on target).

**Spec:** `doc/design/specs/2026-10-08-firmware-lib-design.md`

## Global Constraints

- All code, comments, docs and commit messages in English; commits use `feat:`, `fix:`, `docs:`, `test:`, `chore:`.
- Python line length 100; run `ruff format .` before committing; `ruff check .` and `ruff format --check .` must pass.
- Core C uses `__asm__`, never `asm` (not a keyword with `-std=c99`), and casts pointers to integers via `uintptr_t` (host is 64-bit).
- Core C: C99, no STM32 HAL/CMSIS calls, no weak symbols, no unaligned multi-byte access (use `lib_bytes.h`), no `Error_Handler()`, loops over generated lists use `!=`.
- Core C must compile with `gcc -std=c99 -Wall -Wextra -Werror -fshort-enums` (the generated `reg_map.h` assumes short enums, as arm-none-eabi does by default).
- Wire formats stay compatible: LeBin packets (start byte `0x90`, LE length at bytes 2–3), Modbus register map, Modbus upgrade registers (base 1000, 42 registers).
- Exported files are written with CRLF + UTF-8 via `regmap.writer.encode`.
- Every core file contains exactly one line with the marker `@regmap-lib`; port files contain none.
- Work on `main` (no feature branch), commit after each task.

## Review Focus

1. **Modbus master sending with gaps between bytes (USB-RS485 adapters):** a frame split over several idle events must still be answered once the rest arrives within t3.5 — pinned by the "split frame" test in Task 3.
2. **Garbage or a stale half packet on the LeBin link (cable plugged in mid-transfer, PC tool crash):** the parser must resynchronise on the next start byte and answer the next valid packet — pinned by the garbage-prefix, bad-length and new-packet-timeout tests in Task 4.
3. **Maps with empty generated lists (`CONF_REG_SYNCED_NUMBER` / `CONF_REG_LOGGER_NUMBER` = 0, as in vms1511):** the library must compile warning-free — pinned by building against vms1511 with `-Werror` in Task 2.
4. **Re-export over a project where the developer edited a core file or a port file:** edits must never be silently lost — pinned by the edited-core and kept-port tests in Task 1.
5. **Modbus settings changed over Modbus itself (write `MB_APPLY`):** the response must still go out with the old settings and the UART is reconfigured afterwards — pinned by the reconfigure test in Task 3.

---

## File Structure

```
src/regmap/export_lib.py                 export logic (stamp, hash, write rules)
src/regmap/cli.py                        + export-lib sub-command
src/regmap/firmware_lib/core/            managed by regmap (hash-protected)
  lib_bytes.h                            LE/BE byte helpers
  port.h                                 contract implemented by ports
  configuration.c/h                      register access, flash streams, sync
  modbus_slave.c/h                       Modbus RTU slave
  lebin.c/h                              LeBin protocol
  fw_upgrade.c/h                         shared upgrade session
  mb_upgrade.c/h                         Modbus upgrade registers
src/regmap/firmware_lib/port/            owned by the firmware project
  common.h  regmap_lib_conf.h  system_msp.h  config_app.c/h
  port_stm32.c/h  modbus_port_stm32.c  lebin_port_usb_cdc.c  lebin_port_uart.c
  upgrade_port_stm32.c
tests/test_export_lib.py                 export rules + CLI
tests/test_firmware_core.py              host build + run of the C tests
tests/test_firmware_ports.py             syntax check of STM32 ports against a HAL stub
tests/c/                                 main.h (host), check.h, fake_port.c/h, test_*.c
tests/c/hal_stub/                        main.h, usbd_cdc.h (HAL stand-ins for syntax checks)
doc/firmware-lib.md, doc/firmware-porting.md, doc/cli.md, doc/index.md,
doc/development.md, README.md
```

---

### Task 1: Export command and library foundation

**Files:**
- Create: `src/regmap/export_lib.py`
- Create: `src/regmap/firmware_lib/core/lib_bytes.h`
- Create: `src/regmap/firmware_lib/port/common.h`
- Create: `src/regmap/firmware_lib/port/regmap_lib_conf.h`
- Create: `src/regmap/firmware_lib/port/system_msp.h`
- Modify: `src/regmap/cli.py`
- Test: `tests/test_export_lib.py`

**Interfaces:**
- Produces (Python): `regmap.export_lib.MARKER = "@regmap-lib"`, `LibFile(name: str, core: bool, text: str)`, `library_files() -> list[LibFile]`, `content_hash(text: str) -> str`, `stamp(text: str) -> str`, `is_pristine(text: str) -> bool`, `ExportError(paths: list[Path])`, `export(target: Path, force: bool = False, force_ports: bool = False) -> list[tuple[Path, str]]` with statuses `written | updated | kept | unchanged`.
- Produces (C): `Bytes_GetU16Le/Be`, `Bytes_GetU32Le`, `Bytes_PutU16Le/Be`, `Bytes_PutU32Le` (`lib_bytes.h`); `Status_t`, `STATUS_OK/ERROR/TIMEOUT/BUSY`, `MIN`, `MAX`, `SAT_UP`, `SAT_DOWN`, `UNUSED`, `__weak`, `__packed`, `__aligned` (`common.h`); all `REGMAP_LIB_*`, `LEBIN_*`, `MB_PORT_*`, `UPGRADE_PORT_*` settings (`regmap_lib_conf.h`).

- [ ] **Step 1: Write the failing tests**

`tests/test_export_lib.py`:

```python
import pytest

from regmap import __version__
from regmap.cli import main
from regmap.export_lib import (
    MARKER,
    ExportError,
    export,
    is_pristine,
    library_files,
    stamp,
)
from regmap.writer import encode


def test_library_has_core_and_port_files():
    kinds = {f.name: f.core for f in library_files()}
    assert len(kinds) == len(library_files())  # no duplicate names
    assert kinds["lib_bytes.h"] is True
    assert kinds["common.h"] is False
    assert kinds["regmap_lib_conf.h"] is False


def test_core_files_are_stamped_and_port_files_are_not():
    for f in library_files():
        if f.core:
            assert f"{MARKER} {__version__} sha256:" in f.text, f.name
            assert is_pristine(f.text), f.name
        else:
            assert MARKER not in f.text, f.name


def test_is_pristine_detects_edits():
    text = stamp("/*\n * @regmap-lib\n */\nint a;\n")
    assert is_pristine(text)
    assert is_pristine(text.replace("\n", "\r\n"))  # line endings do not matter
    assert not is_pristine(text.replace("int a;", "int b;"))
    assert not is_pristine("int a;\n")  # no marker at all


def test_first_export_writes_everything_with_crlf(tmp_path):
    target = tmp_path / "lib"
    report = export(target)
    assert len(report) == len(library_files())
    assert {status for _, status in report} == {"written"}
    data = (target / "common.h").read_bytes()
    assert b"\r\n" in data and b"\n" not in data.replace(b"\r\n", b"")


def test_reexport_reports_unchanged(tmp_path):
    export(tmp_path)
    assert {status for _, status in export(tmp_path)} == {"unchanged"}


def test_reexport_keeps_edited_port_file(tmp_path):
    export(tmp_path)
    conf = tmp_path / "regmap_lib_conf.h"
    conf.write_bytes(b"/* project settings */\r\n")
    report = dict(export(tmp_path))
    assert report[conf] == "kept"
    assert conf.read_bytes() == b"/* project settings */\r\n"


def test_force_ports_overwrites_port_file(tmp_path):
    export(tmp_path)
    conf = tmp_path / "regmap_lib_conf.h"
    conf.write_bytes(b"/* project settings */\r\n")
    report = dict(export(tmp_path, force_ports=True))
    assert report[conf] == "updated"
    original = next(f for f in library_files() if f.name == "regmap_lib_conf.h")
    assert conf.read_bytes() == encode(original.text)


def test_reexport_updates_unmodified_older_core_file(tmp_path):
    export(tmp_path)
    core = tmp_path / "lib_bytes.h"
    core.write_bytes(encode(stamp("/*\n * @regmap-lib\n */\n/* older library */\n")))
    report = dict(export(tmp_path))
    assert report[core] == "updated"
    current = next(f for f in library_files() if f.name == "lib_bytes.h")
    assert core.read_bytes() == encode(current.text)


def test_edited_core_file_stops_the_whole_export(tmp_path):
    export(tmp_path)
    core = tmp_path / "lib_bytes.h"
    core.write_bytes(core.read_bytes() + b"/* local change */\r\n")
    (tmp_path / "common.h").unlink()
    with pytest.raises(ExportError) as exc:
        export(tmp_path)
    assert exc.value.paths == [core]
    assert not (tmp_path / "common.h").exists()  # nothing written


def test_force_overwrites_edited_core_file(tmp_path):
    export(tmp_path)
    core = tmp_path / "lib_bytes.h"
    core.write_bytes(b"/* local change */\r\n")
    report = dict(export(tmp_path, force=True))
    assert report[core] == "updated"


def test_cli_export_lib(tmp_path, capsys):
    assert main(["export-lib", str(tmp_path / "fw")]) == 0
    assert capsys.readouterr().out.count("written") == len(library_files())
    assert main(["export-lib", str(tmp_path / "fw")]) == 0
    assert capsys.readouterr().out.count("unchanged") == len(library_files())


def test_cli_export_lib_refuses_edited_core(tmp_path, capsys):
    main(["export-lib", str(tmp_path)])
    core = tmp_path / "lib_bytes.h"
    core.write_bytes(b"/* local change */\r\n")
    capsys.readouterr()
    assert main(["export-lib", str(tmp_path)]) == 1
    err = capsys.readouterr().err
    assert "error: 1 library file(s) were edited by hand, nothing written" in err
    assert str(core) in err
    assert "--force" in err
    assert main(["export-lib", str(tmp_path), "--force"]) == 0


def test_cli_export_lib_target_is_a_file(tmp_path, capsys):
    target = tmp_path / "file"
    target.write_text("x")
    assert main(["export-lib", str(target)]) == 1
    assert capsys.readouterr().err.startswith("error: ")
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `pytest tests/test_export_lib.py -q`
Expected: FAIL with `ModuleNotFoundError: No module named 'regmap.export_lib'`

- [ ] **Step 3: Write `src/regmap/export_lib.py`**

```python
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


def export(
    target: Path, force: bool = False, force_ports: bool = False
) -> list[tuple[Path, str]]:
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
```

- [ ] **Step 4: Add the CLI sub-command in `src/regmap/cli.py`**

Add the import next to the other `regmap` imports:

```python
from regmap.export_lib import ExportError, export
```

Add the handler after `_import_xlsx`:

```python
def _export_lib(args: argparse.Namespace) -> int:
    try:
        report = export(args.dir, force=args.force, force_ports=args.force_ports)
    except ExportError as exc:
        _error(f"{len(exc.paths)} library file(s) were edited by hand, nothing written:")
        for path in exc.paths:
            print(f"  {path}", file=sys.stderr)
        print("use --force to overwrite them", file=sys.stderr)
        return 1
    for path, status in report:
        print(f"{status:9} {path}")
    return 0
```

Register it in `build_parser()` before the `schema` sub-parser:

```python
    exp = sub.add_parser("export-lib", help="write the firmware communication library")
    exp.add_argument("dir", type=Path, help="target directory in the firmware project")
    exp.add_argument("--force", action="store_true", help="overwrite edited library core files")
    exp.add_argument("--force-ports", action="store_true", help="overwrite port/config files")
    exp.set_defaults(func=_export_lib)
```

Update the module docstring to `"""Command line interface: regmap generate | init | import-xlsx | export-lib | schema."""`.

- [ ] **Step 5: Create `src/regmap/firmware_lib/core/lib_bytes.h`**

```c
/**
 * @file       lib_bytes.h
 * @brief      Byte order helpers for protocol buffers
 * @regmap-lib
 *
 * Packet fields are read and written byte by byte, so buffers may be unaligned (Cortex-M0/M0+
 * fault on unaligned word access).
 */
#ifndef LIB_BYTES_H_
#define LIB_BYTES_H_

#include <stdint.h>

static inline uint16_t Bytes_GetU16Le(const uint8_t *p)
{
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline uint16_t Bytes_GetU16Be(const uint8_t *p)
{
  return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static inline uint32_t Bytes_GetU32Le(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void Bytes_PutU16Le(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static inline void Bytes_PutU16Be(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)v;
}

static inline void Bytes_PutU32Le(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

#endif /* LIB_BYTES_H_ */
```

- [ ] **Step 6: Create `src/regmap/firmware_lib/port/common.h`**

```c
/**
 * @file       common.h
 * @brief      Common definitions for all modules
 *
 * Port file: written once by `regmap export-lib`, then owned by the project. If the project
 * already has its own common.h, put its content here (it must keep the definitions below).
 */
#ifndef APPLICATION_COMMON_H_
#define APPLICATION_COMMON_H_

/* Includes ------------------------------------------------------------------*/

#include "main.h" /* CubeMX main.h includes the HAL header of the MCU family */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Definitions----------------------------------------------------------------*/

#define STATUS_OK       0   ///< Success
#define STATUS_ERROR    1   ///< Error or fail
#define STATUS_TIMEOUT  2   ///< Timeout
#define STATUS_BUSY     3   ///< Busy, try again later

/* Macros --------------------------------------------------------------------*/

#ifndef MIN
#define MIN(a, b)   (((a)>(b))?(b):(a))
#endif

#ifndef MAX
#define MAX(a, b)   (((a)<(b))?(b):(a))
#endif

/** Saturate x to the upper bound val */
#define SAT_UP(x, val)      ((x) = ((x)>(val))?(val):(x))

/** Saturate x to the lower bound val */
#define SAT_DOWN(x, val)    ((x) = ((x)<(val))?(val):(x))

#ifndef UNUSED
#define UNUSED(x)   ((void)(x))
#endif

#ifndef __weak
#define __weak      __attribute__((weak))
#endif

#ifndef __packed
#define __packed    __attribute__((packed))
#endif

#ifndef __aligned
#define __aligned(x) __attribute__((aligned(x)))
#endif

/** True when the HAL tick has passed the time stamp a */
#define TICK_EXPIRED(a) (HAL_GetTick() - (a) < 0x7fffffff)

#define GET_BYTE_0(a)       ((uint8_t) ((a) & 0xff))
#define GET_BYTE_1(a)       ((uint8_t) (((a) >> 8) & 0xff))
#define GET_BYTE_2(a)       ((uint8_t) (((a) >> 16) & 0xff))
#define GET_BYTE_3(a)       ((uint8_t) (((a) >> 24) & 0xff))

/** printf-like debug output, disabled by default */
#define PRINTF(...)

#define ASSERT_PARAM(expr)

#define ERR_PRINT(ret, code)  PRINTF("ERR: code [%d] line [%d] file [%s] \n\r", code, __LINE__, __FILE__)

#define CATCH_ERROR(retValue, errorCode)  \
  do {                                    \
    if ((retValue) != 0)                  \
    {                                     \
      ERR_PRINT(retValue, errorCode);     \
    }                                     \
  } while (0)

/* Typedefs-------------------------------------------------------------------*/

/** General pointer to function type */
typedef void (*System_Callback_t)(void);

/** General status return type */
typedef int16_t Status_t;

#endif /* APPLICATION_COMMON_H_ */
```

- [ ] **Step 7: Create `src/regmap/firmware_lib/port/regmap_lib_conf.h`**

```c
/**
 * @file       regmap_lib_conf.h
 * @brief      Settings of the regmap firmware library
 *
 * Port file: written once by `regmap export-lib`, then owned by the project. Every setting can
 * also be given on the compiler command line (-D...).
 */
#ifndef REGMAP_LIB_CONF_H_
#define REGMAP_LIB_CONF_H_

/* Modules: 1 = compiled in, 0 = left out ------------------------------------*/

#ifndef REGMAP_LIB_LEBIN
#define REGMAP_LIB_LEBIN              1
#endif
#ifndef REGMAP_LIB_MODBUS
#define REGMAP_LIB_MODBUS             1
#endif
#ifndef REGMAP_LIB_UPGRADE
#define REGMAP_LIB_UPGRADE            1
#endif

/* LeBin ---------------------------------------------------------------------*/

#ifndef LEBIN_RX_BUFFER_SIZE
#define LEBIN_RX_BUFFER_SIZE          4096u   ///< Received bytes not processed yet (power of two)
#endif
#ifndef LEBIN_PACKET_MAX
#define LEBIN_PACKET_MAX              4096u   ///< Largest request and response packet
#endif
#ifndef LEBIN_NEW_PACKET_MS
#define LEBIN_NEW_PACKET_MS           750u    ///< A partial packet older than this is dropped
#endif

#define LEBIN_PORT_USB_CDC            1
#define LEBIN_PORT_UART               2
#ifndef LEBIN_PORT_TRANSPORT
#define LEBIN_PORT_TRANSPORT          LEBIN_PORT_USB_CDC
#endif
#ifndef LEBIN_PORT_USB_DEVICE
#define LEBIN_PORT_USB_DEVICE         hUsbDeviceFS   ///< CubeMX USB device handle (hUsbDeviceHS for HS)
#endif
#ifndef LEBIN_PORT_UART_HANDLE
#define LEBIN_PORT_UART_HANDLE        huart1         ///< CubeMX UART handle for the UART transport
#endif

/* Modbus port ---------------------------------------------------------------*/

#ifndef MB_PORT_UART_HANDLE
#define MB_PORT_UART_HANDLE           huart2   ///< CubeMX UART handle; pins, IRQ and DMA set in CubeMX
#endif
#ifndef MB_PORT_USE_DMA
#define MB_PORT_USE_DMA               0        ///< 1: DMA reception/transmission, 0: interrupts
#endif
/* RS-485 driver enable pin. Remove both lines for RS-232 or when MB_PORT_HW_DE is used. */
#define MB_PORT_DE_GPIO_PORT          GPIOA
#define MB_PORT_DE_GPIO_PIN           GPIO_PIN_1
/* Hardware driver enable of the USART (DE pin configured in CubeMX): */
/* #define MB_PORT_HW_DE              1 */

/* HAL UART callbacks --------------------------------------------------------*/

/* 1: port_stm32.c defines HAL_UARTEx_RxEventCallback, HAL_UART_TxCpltCallback and
 * HAL_UART_ErrorCallback. 0: the project defines them and calls PortStm32_UartRxEvent,
 * PortStm32_UartTxCplt and PortStm32_UartError from them. */
#ifndef REGMAP_LIB_HAL_UART_CALLBACKS
#define REGMAP_LIB_HAL_UART_CALLBACKS 1
#endif

/* Firmware upgrade port -----------------------------------------------------*/

/* Flash area receiving the new image; linker symbols from configuration.h. The start must be
 * aligned to the erase unit (sector or page). */
#ifndef UPGRADE_PORT_AREA_ADDRESS
#define UPGRADE_PORT_AREA_ADDRESS     ((uint32_t)(uintptr_t)CONF_C_APP_BUFFER_OFFSET)
#endif
#ifndef UPGRADE_PORT_AREA_SIZE
#define UPGRADE_PORT_AREA_SIZE        ((uint32_t)(uintptr_t)CONF_C_APPLICATION_MAX_SIZE)
#endif
/* Image check after the last packet, e.g. System_VerifyImage((uint32_t *)(address)) */
#ifndef UPGRADE_PORT_VERIFY
#define UPGRADE_PORT_VERIFY(address, size)  (STATUS_OK)
#endif

#endif /* REGMAP_LIB_CONF_H_ */
```

- [ ] **Step 8: Create `src/regmap/firmware_lib/port/system_msp.h`**

```c
/**
 * @file       system_msp.h
 * @brief      System services header included by the generated mb_rtu_app.c
 *
 * Port file: a minimal stand-in. If the project has its own system_msp.h, put its content
 * here.
 */
#ifndef SYSTEM_MSP_H_
#define SYSTEM_MSP_H_

#include "common.h"

#endif /* SYSTEM_MSP_H_ */
```

- [ ] **Step 9: Run tests to verify they pass**

Run: `pytest tests/test_export_lib.py -q`
Expected: PASS (13 tests)

- [ ] **Step 10: Lint and commit**

```bash
ruff check . && ruff format --check .
pytest -q
git add src/regmap/export_lib.py src/regmap/cli.py src/regmap/firmware_lib tests/test_export_lib.py
git commit -m "feat: regmap export-lib command and firmware library foundation"
```

---

### Task 2: Host C test harness and register access module

**Files:**
- Create: `src/regmap/firmware_lib/core/port.h`
- Create: `src/regmap/firmware_lib/core/configuration.h`
- Create: `src/regmap/firmware_lib/core/configuration.c`
- Create: `src/regmap/firmware_lib/port/config_app.h`
- Create: `src/regmap/firmware_lib/port/config_app.c`
- Create: `tests/c/main.h`, `tests/c/check.h`, `tests/c/fake_port.h`, `tests/c/fake_port.c`, `tests/c/test_main.c`, `tests/c/test_configuration.c`
- Test: `tests/test_firmware_core.py`

**Interfaces:**
- Consumes: `lib_bytes.h`, `common.h`, `regmap_lib_conf.h` (Task 1); generated `reg_map.h/.c` (`conf`, `CONF_REG`, `CONF_REG_LIMIT`, `CONF_REG_FLASH`, `CONF_REG_SYNCED`, `*_NUMBER`, `CONF_DIM_CONDITION`, `RegMap_RestoreFactoryValues`).
- Produces (C): `port.h` with `uint32_t Port_GetTickMs(void)`, `void Port_CriticalEnter(void)`, `void Port_CriticalExit(void)`; `configuration.h` with `CONF_PTR(id)`, `CONF_BYTE_LEN_ID(id)`, `CONF_INT/SHORT/BYTE/FLOAT(id)`, `Status_t Config_Init(void)`, `Status_t Config_CheckLimits(uint32_t id)`, `Status_t Config_ApplyConfig(uint32_t id)`, `Status_t Config_ReadStream(const uint8_t *data, uint32_t length)`, `Status_t Config_FillStream(uint8_t *data, uint32_t *length, uint32_t maxLength)`, `Status_t Config_NeedToSync(uint8_t *data, uint16_t *length)`; `config_app.h` with `Status_t Config_AppInit(void)`, `Status_t Config_Callback(uint32_t id)`.
- Produces (tests): `uint32_t fake_tick` (`fake_port.h`), `CHECK(cond)`, `CHECK_BYTES(actual, len, ...)` (`check.h`), test runner `tests/c/test_main.c` calling `void test_<module>(void)` functions.

- [ ] **Step 1: Write the pytest driver `tests/test_firmware_core.py`**

```python
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
        run(["gcc", *FLAGS, *(["-Werror"] if strict else []), *includes, "-c", str(source), "-o", str(target)])
        objects.append(str(target))
    binary = tmp / ("core_tests.exe" if os.name == "nt" else "core_tests")
    run(["gcc", *objects, "-o", str(binary)])
    return binary


def test_core_library(core_binary):
    result = subprocess.run([str(core_binary)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "all checks passed" in result.stdout
```

- [ ] **Step 2: Create the host test support in `tests/c/`**

`tests/c/main.h` (host stand-in for the CubeMX `main.h`):

```c
/* Host stand-in for the CubeMX main.h: only what common.h and the generated sources use. */
#ifndef MAIN_H_
#define MAIN_H_

#include <stdint.h>

#define __packed            __attribute__((packed))
#define __aligned(x)        __attribute__((aligned(x)))
#define CONF_SECTION(name)  /* no linker sections on the host */

static inline uint32_t __REV16(uint32_t v)
{
  return ((v & 0x00FF00FFu) << 8) | ((v & 0xFF00FF00u) >> 8);
}

uint32_t HAL_GetTick(void);

#endif /* MAIN_H_ */
```

`tests/c/check.h`:

```c
/* Minimal assertion helpers for the host C tests. */
#ifndef CHECK_H_
#define CHECK_H_

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern int check_failures;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond))                                                          \
    {                                                                     \
      check_failures++;                                                   \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
    }                                                                     \
  } while (0)

/* Compare `len` bytes at `actual` with the listed bytes (length must match too). */
#define CHECK_BYTES(actual, len, ...)                                     \
  do {                                                                    \
    const uint8_t expected_[] = {__VA_ARGS__};                            \
    CHECK((size_t)(len) == sizeof(expected_)                              \
          && memcmp((actual), expected_, sizeof(expected_)) == 0);        \
  } while (0)

#endif /* CHECK_H_ */
```

`tests/c/fake_port.h`:

```c
/* Fake port for the host tests: simulated tick, captured transmissions, RAM flash. */
#ifndef FAKE_PORT_H_
#define FAKE_PORT_H_

#include "port.h"

/* General */
extern uint32_t fake_tick;

#endif /* FAKE_PORT_H_ */
```

`tests/c/fake_port.c`:

```c
#include "fake_port.h"

/* General -------------------------------------------------------------------*/

uint32_t fake_tick = 1000;

uint32_t Port_GetTickMs(void)
{
  return fake_tick;
}

uint32_t HAL_GetTick(void)
{
  return fake_tick;
}

void Port_CriticalEnter(void)
{
}

void Port_CriticalExit(void)
{
}
```

`tests/c/test_main.c`:

```c
#include "check.h"

int check_failures;

void test_configuration(void);

int main(void)
{
  test_configuration();

  if (check_failures != 0)
  {
    printf("%d check(s) failed\n", check_failures);
    return 1;
  }
  printf("all checks passed\n");
  return 0;
}
```

- [ ] **Step 3: Write the failing C tests `tests/c/test_configuration.c`**

```c
#include "check.h"
#include "configuration.h"
#include "lib_bytes.h"

static void test_init_checks_layout(void)
{
  CHECK(Config_Init() == STATUS_OK); /* fails when reg_map.h and the compiler disagree */
  CHECK(conf.com.mb_timeout == 10);  /* factory value of vms1511 */
}

static void test_check_limits(void)
{
  CHECK(Config_CheckLimits(CONF_SYS_UPTIME) == STATUS_OK);
  CHECK(Config_CheckLimits(CONF_COM_MB_TIMEOUT) == STATUS_OK);
  CHECK(Config_CheckLimits(0x7F000012u) != STATUS_OK);  /* block beyond the map */
  CHECK(Config_CheckLimits(0x00100012u) != STATUS_OK);  /* address 256 beyond block SYS */
}

static void test_stream_round_trip_on_unaligned_buffer(void)
{
  uint8_t buffer[CONF_REG_FLASH_LENGTH + 64];
  uint8_t *stream = buffer + 1; /* deliberately unaligned */
  uint32_t length = 0;

  Config_Init();
  conf.com.mb_timeout = 0x1234;
  CHECK(Config_FillStream(stream, &length, sizeof(buffer) - 1) == STATUS_OK);
  CHECK(length == CONF_REG_FLASH_LENGTH);
  CHECK(Bytes_GetU32Le(stream) == CONF_SYS_REGMAP_VERSION);

  conf.com.mb_timeout = 0;
  CHECK(Config_ReadStream(stream, length) == STATUS_OK);
  CHECK(conf.com.mb_timeout == 0x1234);
}

static void test_fill_stream_reports_small_buffer(void)
{
  uint8_t buffer[8];
  uint32_t length = 0;

  CHECK(Config_FillStream(buffer, &length, sizeof(buffer)) == STATUS_ERROR);
  CHECK(length == 8); /* the version entry fits, the rest does not */
}

static void test_read_stream_rejects_other_major_version(void)
{
  uint8_t stream[8];

  Config_Init();
  Bytes_PutU32Le(stream, CONF_SYS_REGMAP_VERSION);
  Bytes_PutU32Le(stream + 4, CONF_INT(CONF_SYS_REGMAP_VERSION) + 0x00010000u);
  CHECK(Config_ReadStream(stream, sizeof(stream)) == STATUS_ERROR);
}

static void test_read_stream_skips_unknown_and_stops_on_truncation(void)
{
  uint8_t stream[64];
  uint32_t idx = 0;

  Config_Init();
  Bytes_PutU32Le(stream + idx, CONF_SYS_REGMAP_VERSION);
  Bytes_PutU32Le(stream + idx + 4, CONF_INT(CONF_SYS_REGMAP_VERSION));
  idx += 8;
  Bytes_PutU32Le(stream + idx, CONF_SYS_UPTIME); /* not a flash register: skipped */
  Bytes_PutU32Le(stream + idx + 4, 77);
  idx += 8;
  Bytes_PutU32Le(stream + idx, CONF_COM_MB_TIMEOUT);
  Bytes_PutU16Le(stream + idx + 4, 42);
  idx += 6;
  conf.sys.uptime = 5;
  CHECK(Config_ReadStream(stream, idx) == STATUS_OK);
  CHECK(conf.sys.uptime == 5);
  CHECK(conf.com.mb_timeout == 42);
  CHECK(Config_ReadStream(stream, idx - 1) == STATUS_ERROR); /* last value truncated */
}

static void test_need_to_sync_without_synced_registers(void)
{
  uint8_t data[16];
  uint16_t length = 99;

  CHECK(Config_NeedToSync(data, &length) == STATUS_ERROR); /* vms1511 has none */
  CHECK(length == 0);
}

void test_configuration(void)
{
  test_init_checks_layout();
  test_check_limits();
  test_stream_round_trip_on_unaligned_buffer();
  test_fill_stream_reports_small_buffer();
  test_read_stream_rejects_other_major_version();
  test_read_stream_skips_unknown_and_stops_on_truncation();
  test_need_to_sync_without_synced_registers();
}
```

- [ ] **Step 4: Run to verify it fails**

Run: `pytest tests/test_firmware_core.py -q`
Expected: FAIL — compile error `port.h: No such file or directory` (or SKIP if `gcc` is missing; then install gcc — MinGW on Windows, `build-essential` on Linux — before continuing).

- [ ] **Step 5: Create `src/regmap/firmware_lib/core/port.h`**

```c
/**
 * @file       port.h
 * @brief      Interface between the regmap library core and the MCU port
 * @regmap-lib
 *
 * The core calls these functions; the port (port_stm32.c, modbus_port_stm32.c, ...) defines
 * them. "ISR" marks functions that may be called from interrupt context.
 */
#ifndef PORT_H_
#define PORT_H_

#include "common.h"
#include "regmap_lib_conf.h"

/* General -------------------------------------------------------------------*/

/** Millisecond tick, wraps around at 2^32 */
uint32_t Port_GetTickMs(void);

/** Enter a short critical section (interrupts off); calls may nest */
void Port_CriticalEnter(void);

/** Leave the critical section entered by Port_CriticalEnter */
void Port_CriticalExit(void);

#endif /* PORT_H_ */
```

- [ ] **Step 6: Create `src/regmap/firmware_lib/core/configuration.h`**

```c
/**
 * @file       configuration.h
 * @brief      Configuration and tools for register map access
 * @regmap-lib
 *
 * @defgroup grConfig Configuration
 * @{
 * @brief Tools for accessing configuration and register map
 *
 * @par Main features:
 * - Access macros to the registers according to their type
 * - Flash storage streams
 * - Callback for register change (Config_Callback in config_app.c)
 * - Synchronisation stream
 */
#ifndef CONFIGURATION_H_
#define CONFIGURATION_H_

/* Includes ------------------------------------------------------------------*/

#include "common.h"
#include "reg_map.h"

/* Macros --------------------------------------------------------------------*/

/** Block number of the register */
#define CONF_BLOCK_ID(id) (((id) & 0xFF000000) >> 24)

/** Address of the register within the block */
#define CONF_ADDR_ID(id)  (((id) & 0x00FFF000) >> 12)

/** Byte length of the register */
#define CONF_BYTE_LEN_ID(id)  (CONF_LENGTH[((id) & 0x0000000F)])

/** Variable type of the register */
#define CONF_TYPE_ID(id)    (((id) & 0x0F00) >> 8)

/** uint8_t pointer to the beginning of the register */
#define CONF_PTR(id) (CONF_REG[CONF_BLOCK_ID(id)] + CONF_ADDR_ID(id))

/** Get/set uint32_t value of the register */
#define CONF_INT(id) (*((uint32_t *)(CONF_REG[CONF_BLOCK_ID(id)] + CONF_ADDR_ID(id))))

/** Get/set uint16_t value of the register */
#define CONF_SHORT(id) (*((uint16_t *)(CONF_REG[CONF_BLOCK_ID(id)] + CONF_ADDR_ID(id))))

/** Get/set uint8_t value of the register */
#define CONF_BYTE(id) (*((uint8_t *)(CONF_REG[CONF_BLOCK_ID(id)] + CONF_ADDR_ID(id))))

/** Get/set float value of the register */
#define CONF_FLOAT(id) (*((float *)(CONF_REG[CONF_BLOCK_ID(id)] + CONF_ADDR_ID(id))))

/* Circuit-dependent macros (blocks repeated per circuit) */

#define CONF_PTR_C(id, c) (CONF_REG[CONF_BLOCK_ID(id) + (c)] + CONF_ADDR_ID(id))
#define CONF_INT_C(id, c) (*((uint32_t *)(CONF_REG[CONF_BLOCK_ID(id) + (c)] + CONF_ADDR_ID(id))))
#define CONF_SHORT_C(id, c) (*((uint16_t *)(CONF_REG[CONF_BLOCK_ID(id) + (c)] + CONF_ADDR_ID(id))))
#define CONF_BYTE_C(id, c) (*((uint8_t *)(CONF_REG[CONF_BLOCK_ID(id) + (c)] + CONF_ADDR_ID(id))))
#define CONF_FLOAT_C(id, c) (*((float *)(CONF_REG[CONF_BLOCK_ID(id) + (c)] + CONF_ADDR_ID(id))))
#define CONF_ID_C(id, c)  ((id) + ((c) * 0x01000000))

/* Array access macros */

#define CONF_FLOAT_A(id, a) (*((float *)(CONF_REG[CONF_BLOCK_ID(id)] + CONF_ADDR_ID(id) + (a) * sizeof(float))))
#define CONF_INT_A(id, a) (*((uint32_t *)(CONF_REG[CONF_BLOCK_ID(id)] + CONF_ADDR_ID(id) + (a) * sizeof(uint32_t))))
#define CONF_SHORT_A(id, a) (*((uint16_t *)(CONF_REG[CONF_BLOCK_ID(id)] + CONF_ADDR_ID(id) + (a) * sizeof(uint16_t))))
#define CONF_BYTE_A(id, a) (*((uint8_t *)(CONF_REG[CONF_BLOCK_ID(id)] + CONF_ADDR_ID(id) + (a) * sizeof(uint8_t))))

/* Constants -----------------------------------------------------------------*/

/* Defined in reg_map.c */
extern conf_reg_t conf;
extern uint8_t* const CONF_REG[CONF_REG_BLOCK_NUMBER];
extern const uint32_t CONF_REG_LIMIT[CONF_REG_BLOCK_NUMBER];
extern const uint32_t CONF_REG_FLASH[CONF_REG_FLASH_NUMBER];
extern const uint32_t CONF_REG_LOGGER[CONF_REG_LOGGER_NUMBER];
extern const uint32_t CONF_REG_CALIB[CONF_REG_CALIB_NUMBER];
extern const uint32_t CONF_REG_SYNCED[CONF_REG_SYNCED_NUMBER];

/* Defined in configuration.c */
extern const uint32_t CONF_FIRMWARE_INFO[8];
extern const uint32_t CONF_FIRMWARE_INFO_DEFAULT[8];
extern const uint32_t CONF_LENGTH[16];

/* Defined by the linker script (needed only by the firmware upgrade port) */
extern const uint8_t CONF_FW_INFO_OFFSET[]          __asm__("_LD_FW_INFO_OFFSET");
extern const uint8_t CONF_C_BOOTLOADER_OFFSET[]     __asm__("_LD_ADDRESS_BOOTLOADER");
extern const uint8_t CONF_C_APPLICATION_OFFSET[]    __asm__("_LD_ADDRESS_APPLICATION");
extern const uint8_t CONF_C_CALIBRATION_OFFSET[]    __asm__("_LD_ADDRESS_CALIBRATION");
extern const uint8_t CONF_C_APP_BUFFER_OFFSET[]     __asm__("_LD_ADDRESS_BUFFER_APP");
extern const uint8_t CONF_C_APPLICATION_MAX_SIZE[]  __asm__("_LD_SIZE_BUFFER_APP");

/* Functions -----------------------------------------------------------------*/

/**
 * Check the register map layout, set factory values and call Config_AppInit.
 * @return STATUS_ERROR when reg_map.h does not match the compiler's structure layout
 */
Status_t Config_Init(void);

/**
 * Check that the register ID lies within the register map.
 * @param id Register ID
 * @return STATUS_OK if the ID is within limits
 */
Status_t Config_CheckLimits(uint32_t id);

/**
 * Notify about a changed register value; calls Config_Callback (config_app.c). Registers with
 * the flash flag ((id & 0x070) == 0x070) are to be stored by the application there.
 * @param id ID of the modified register
 * @return Status of Config_Callback
 */
Status_t Config_ApplyConfig(uint32_t id);

/**
 * Copy flash registers from a stored stream (ID, value pairs) into the register map. The
 * first entry must be CONF_SYS_REGMAP_VERSION with the same major part as the factory value;
 * unknown IDs are skipped.
 * @param data Stream
 * @param length Length of the stream in bytes
 * @return STATUS_ERROR for a different map version or a truncated stream
 */
Status_t Config_ReadStream(const uint8_t *data, uint32_t length);

/**
 * Create a stream of all flash registers for storage.
 * @param data Buffer for the stream
 * @param length Resulting length of the stream
 * @param maxLength Size of the buffer
 * @return STATUS_ERROR if the buffer is too small (the stream holds what fitted)
 */
Status_t Config_FillStream(uint8_t *data, uint32_t *length, uint32_t maxLength);

/**
 * Create a stream of synchronised registers that changed since the last call.
 * @param data Buffer for the stream
 * @param length Resulting length of the stream
 * @return STATUS_OK if at least one register changed
 */
Status_t Config_NeedToSync(uint8_t *data, uint16_t *length);

#endif /* CONFIGURATION_H_ */
/** @} */
```

- [ ] **Step 7: Create `src/regmap/firmware_lib/core/configuration.c`**

```c
/**
 * @file       configuration.c
 * @brief      Configuration and tools for register map access
 * @regmap-lib
 * @addtogroup grConfig
 * @{
 */

/* Includes ------------------------------------------------------------------*/

#include "configuration.h"
#include "config_app.h"
#include "lib_bytes.h"

/* Private macros ------------------------------------------------------------*/

#ifndef CONF_SECTION
#define CONF_SECTION(name) __attribute__((section(name)))
#endif

/* Constants -----------------------------------------------------------------*/

/** Firmware information block in the flash memory */
const uint32_t CONF_SECTION(".sectionFwInfo") CONF_FIRMWARE_INFO[8] = {
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};

/** Empty (default) firmware information block, used for comparison */
const uint32_t CONF_SECTION(".sectionEndOfFlash") CONF_FIRMWARE_INFO_DEFAULT[8] = {
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};

/** Register lengths by the length code in the ID */
const uint32_t CONF_LENGTH[16] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192,
                                  16384, 32768};

/* Private variables ---------------------------------------------------------*/

#if CONF_REG_SYNCED_NUMBER > 0
/** Last synchronised values */
static uint8_t CONF_REG_SYN_LOCAL[CONF_REG_LOCAL_LENGTH];
#endif

/* Functions -----------------------------------------------------------------*/

Status_t Config_Init(void)
{
  if (CONF_DIM_CONDITION)
  {
    return STATUS_ERROR;
  }

  RegMap_RestoreFactoryValues();

  return Config_AppInit();
}


Status_t Config_CheckLimits(uint32_t id)
{
  Status_t ret = STATUS_ERROR;

  if (CONF_BLOCK_ID(id) < CONF_REG_BLOCK_NUMBER)
  {
    if (CONF_REG_LIMIT[CONF_BLOCK_ID(id)] >= (CONF_ADDR_ID(id) + CONF_BYTE_LEN_ID(id)))
    {
      ret = STATUS_OK;
    }
  }

  return ret;
}


Status_t Config_ApplyConfig(uint32_t id)
{
  return Config_Callback(id);
}


Status_t Config_ReadStream(const uint8_t *data, uint32_t length)
{
  uint32_t idx = 0;
  uint32_t id;
  uint32_t size;
  uint32_t i;
  bool known;

  /* First entry: map version with the same major part as the factory value */
  if (length < 8 || Bytes_GetU32Le(data) != CONF_SYS_REGMAP_VERSION
      || (Bytes_GetU32Le(data + 4) & 0xFFFF0000u)
         != (CONF_INT(CONF_SYS_REGMAP_VERSION) & 0xFFFF0000u))
  {
    return STATUS_ERROR;
  }

  while (idx + 4 <= length)
  {
    id = Bytes_GetU32Le(data + idx);
    size = CONF_BYTE_LEN_ID(id);
    if (idx + 4 + size > length)
    {
      return STATUS_ERROR;
    }

    known = false;
    for (i = 0; i != CONF_REG_FLASH_NUMBER; i++)
    {
      if (CONF_REG_FLASH[i] == id)
      {
        known = true;
      }
    }
    if (known)
    {
      memcpy(CONF_PTR(id), data + idx + 4, size);
    }

    idx += 4 + size;
  }

  return STATUS_OK;
}


Status_t Config_FillStream(uint8_t *data, uint32_t *length, uint32_t maxLength)
{
  Status_t ret = STATUS_OK;
  uint32_t idx = 0;
  uint32_t id;
  uint32_t size;
  uint32_t i;

  for (i = 0; i != CONF_REG_FLASH_NUMBER; i++)
  {
    id = CONF_REG_FLASH[i];
    size = CONF_BYTE_LEN_ID(id);
    if (idx + 4 + size > maxLength)
    {
      ret = STATUS_ERROR;
      break;
    }
    Bytes_PutU32Le(data + idx, id);
    memcpy(data + idx + 4, CONF_PTR(id), size);
    idx += 4 + size;
  }

  *length = idx;
  return ret;
}


Status_t Config_NeedToSync(uint8_t *data, uint16_t *length)
{
  Status_t ret = STATUS_ERROR;
  uint32_t packetIdx = 0;
#if CONF_REG_SYNCED_NUMBER > 0
  uint32_t tempIdx = 0;
  uint32_t id;
  uint32_t size;
  uint32_t i;

  for (i = 0; i != CONF_REG_SYNCED_NUMBER; i++)
  {
    id = CONF_REG_SYNCED[i];
    size = CONF_BYTE_LEN_ID(id);
    if (memcmp(CONF_PTR(id), CONF_REG_SYN_LOCAL + tempIdx, size) != 0)
    {
      Bytes_PutU32Le(data + packetIdx, id);
      memcpy(data + packetIdx + 4, CONF_PTR(id), size);
      memcpy(CONF_REG_SYN_LOCAL + tempIdx, CONF_PTR(id), size);
      packetIdx += 4 + size;
      ret = STATUS_OK;
    }
    tempIdx += size;
  }
#else
  UNUSED(data);
#endif

  *length = (uint16_t)packetIdx;
  return ret;
}

/** @} */
```

- [ ] **Step 8: Create `src/regmap/firmware_lib/port/config_app.h` and `config_app.c`**

`config_app.h`:

```c
/**
 * @file       config_app.h
 * @brief      Application hooks of the configuration module
 *
 * Port file: written once by `regmap export-lib`, then owned by the project.
 */
#ifndef CONFIG_APP_H_
#define CONFIG_APP_H_

#include "common.h"
#include "regmap_lib_conf.h"

/**
 * Application initialisation after the factory values are set (e.g. read stored registers
 * from flash with Config_ReadStream).
 * @return Status
 */
Status_t Config_AppInit(void);

/**
 * Called by Config_ApplyConfig after a register was written (LeBin, Modbus, application).
 * @param id ID of the modified register
 * @return Status
 */
Status_t Config_Callback(uint32_t id);

#endif /* CONFIG_APP_H_ */
```

`config_app.c`:

```c
/**
 * @file       config_app.c
 * @brief      Application hooks of the configuration module
 *
 * Port file: written once by `regmap export-lib`, then owned by the project.
 */

#include "config_app.h"
#include "configuration.h"

Status_t Config_AppInit(void)
{
  return STATUS_OK;
}


Status_t Config_Callback(uint32_t id)
{
  UNUSED(id);
  return STATUS_OK;
}
```

- [ ] **Step 9: Run tests to verify they pass**

Run: `pytest tests/test_firmware_core.py tests/test_export_lib.py -q`
Expected: PASS. If gcc reports a warning in a library file, fix the library file (it is compiled with `-Werror`); never relax the flags.

- [ ] **Step 10: Lint and commit**

```bash
ruff check . && ruff format --check .
git add src/regmap/firmware_lib tests/c tests/test_firmware_core.py
git commit -m "feat: register access module of the firmware library with host C tests"
```

---

### Task 3: Modbus RTU slave core

**Files:**
- Create: `src/regmap/firmware_lib/core/modbus_slave.h`
- Create: `src/regmap/firmware_lib/core/modbus_slave.c`
- Modify: `src/regmap/firmware_lib/core/port.h` (Modbus section)
- Modify: `src/regmap/firmware_lib/port/config_app.h`, `config_app.c` (Modbus mapping, timeout hook)
- Modify: `tests/c/fake_port.h`, `tests/c/fake_port.c`, `tests/c/test_main.c`
- Create: `tests/c/test_modbus.c`

**Interfaces:**
- Consumes: generated `mb_rtu_app.h` (`MB_HOLD_LAST`, `MB_INPUT_LAST` — highest address, −1 when empty; `Status_t MbRtu_ReadHoldingRegCallback(uint16_t address, uint16_t *value)`, `MbRtu_ReadInputRegCallback(...)` — `*value` returned byte-swapped for the wire; `Status_t MbRtu_WriteHoldingRegCallback(uint16_t address, uint16_t value)` — native value); `Port_*` (Task 2).
- Produces: `MBSLAVE_FRAME_MAX` (256), `MbSlave_Parity_t` (`MBSLAVE_PARITY_NONE=0/EVEN=1/ODD=2`), `MbSlave_Config_t {uint8_t address; uint32_t baudRate; uint8_t parity; uint8_t stopBits; uint32_t timeoutMs;}`, `Status_t MbSlave_Init(const MbSlave_Config_t *cfg)`, `Status_t MbSlave_Reconfigure(const MbSlave_Config_t *cfg)`, `Status_t MbSlave_Handle(void)`, `bool MbSlave_IsTimeout(void)`, `uint32_t MbSlave_GetErrorCount(void)`, `uint16_t MbSlave_Crc16(const uint8_t *data, uint16_t len)` (low byte is sent first), `void MbSlave_RxFrame(const uint8_t *data, uint16_t len)` (ISR), `void MbSlave_TxDone(void)` (ISR), hook `void MbSlave_TimeoutChanged(bool timeout)` (defined in `config_app.c`); port: `Status_t MbPort_Init(const MbSlave_Config_t *cfg)`, `Status_t MbPort_Send(const uint8_t *data, uint16_t len)`, `void MbPort_NotifyFromIsr(void)`; app: `void ConfigApp_ModbusConfig(MbSlave_Config_t *cfg)`.
- Task 5 adds `#if REGMAP_LIB_UPGRADE` blocks to `modbus_slave.c`; keep the marked spots.

- [ ] **Step 1: Extend the fake port**

Append to `tests/c/fake_port.h` before `#endif`:

```c
/* Modbus */
#include "modbus_slave.h"

extern MbSlave_Config_t fake_mb_cfg;      /* last configuration given to MbPort_Init */
extern int fake_mb_init_count;
extern uint8_t fake_mb_tx[MBSLAVE_FRAME_MAX];
extern uint16_t fake_mb_tx_len;
extern int fake_mb_tx_count;

void fake_mb_reset(void);
```

Append to `tests/c/fake_port.c`:

```c
/* Modbus --------------------------------------------------------------------*/

MbSlave_Config_t fake_mb_cfg;
int fake_mb_init_count;
uint8_t fake_mb_tx[MBSLAVE_FRAME_MAX];
uint16_t fake_mb_tx_len;
int fake_mb_tx_count;

Status_t MbPort_Init(const MbSlave_Config_t *cfg)
{
  fake_mb_cfg = *cfg;
  fake_mb_init_count++;
  return STATUS_OK;
}

Status_t MbPort_Send(const uint8_t *data, uint16_t len)
{
  memcpy(fake_mb_tx, data, len);
  fake_mb_tx_len = len;
  fake_mb_tx_count++;
  return STATUS_OK;
}

void MbPort_NotifyFromIsr(void)
{
}

void fake_mb_reset(void)
{
  fake_mb_init_count = 0;
  fake_mb_tx_len = 0;
  fake_mb_tx_count = 0;
}
```

In `tests/c/test_main.c` declare `void test_modbus(void);` and call `test_modbus();` after `test_configuration();`.

- [ ] **Step 2: Write the failing tests `tests/c/test_modbus.c`**

```c
#include "check.h"
#include "config_app.h"
#include "configuration.h"
#include "fake_port.h"
#include "lib_bytes.h"
#include "mb_rtu_app.h"
#include "modbus_slave.h"

#define HI(x) ((uint8_t)((x) >> 8))
#define LO(x) ((uint8_t)(x))

/* Slave 1 at 19200 Bd; timeoutS is the MB_TIMEOUT register value in seconds. */
static void mb_setup(uint16_t timeoutS)
{
  MbSlave_Config_t cfg;

  fake_tick = 1000;
  fake_mb_reset();
  Config_Init();
  conf.com.mb_address = 1;
  conf.com.mb_baud_rate = MB_BAUD_19200;
  conf.com.mb_timeout = timeoutS;
  ConfigApp_ModbusConfig(&cfg);
  MbSlave_Init(&cfg);
}

/* Append the CRC to `frame` (room for 2 more bytes needed); returns the new length. */
static uint16_t mb_add_crc(uint8_t *frame, uint16_t len)
{
  Bytes_PutU16Le(frame + len, MbSlave_Crc16(frame, len));
  return (uint16_t)(len + 2);
}

/* Receive a request in one idle-line chunk and run the slave once. */
static void mb_request(const uint8_t *pdu, uint16_t len)
{
  uint8_t frame[MBSLAVE_FRAME_MAX];

  memcpy(frame, pdu, len);
  MbSlave_RxFrame(frame, mb_add_crc(frame, len));
  MbSlave_Handle();
}

/* The last response has a valid CRC. */
static bool mb_crc_ok(void)
{
  return fake_mb_tx_len >= 4
         && MbSlave_Crc16(fake_mb_tx, (uint16_t)(fake_mb_tx_len - 2))
            == Bytes_GetU16Le(fake_mb_tx + fake_mb_tx_len - 2);
}

/* Finish the transmission of the last response and run the slave once. */
static void mb_tx_done(void)
{
  MbSlave_TxDone();
  MbSlave_Handle();
}

static void test_crc_known_vector(void)
{
  const uint8_t frame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};

  CHECK(MbSlave_Crc16(frame, sizeof(frame)) == 0xCDC5u); /* sent as C5 CD */
}

static void test_read_holding(void)
{
  const uint8_t req[] = {0x01, 0x03, 0x00, MB_HOLD_COM_MB_TIMEOUT, 0x00, 0x01};

  mb_setup(0);
  conf.com.mb_timeout = 0x1234;
  mb_request(req, sizeof(req));
  CHECK(fake_mb_tx_count == 1);
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x03, 0x02, 0x12, 0x34);
  CHECK(mb_crc_ok());
  mb_tx_done();
}

static void test_read_input_two_words(void)
{
  const uint8_t req[] = {0x01, 0x04, 0x00, MB_INPUT_SYS_UPTIME_0, 0x00, 0x02};

  mb_setup(0);
  conf.sys.uptime = 0x11223344;
  mb_request(req, sizeof(req));
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x04, 0x04, 0x33, 0x44, 0x11, 0x22);
  mb_tx_done();
}

static void test_write_single(void)
{
  const uint8_t req[] = {0x01, 0x06, 0x00, MB_HOLD_COM_MB_TIMEOUT, 0x01, 0x02};

  mb_setup(0);
  mb_request(req, sizeof(req));
  CHECK(conf.com.mb_timeout == 0x0102);
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x06, 0x00, MB_HOLD_COM_MB_TIMEOUT, 0x01, 0x02);
  mb_tx_done();
}

static void test_write_multiple(void)
{
  const uint8_t req[] = {0x01, 0x10, 0x00, MB_HOLD_COM_MB_APPLY, 0x00, 0x02, 0x04,
                         0x00, 0x00, 0x00, 0x05};

  mb_setup(0);
  mb_request(req, sizeof(req));
  CHECK(conf.com.mb_apply == 0);
  CHECK(conf.com.mb_timeout == 5);
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x10, 0x00, MB_HOLD_COM_MB_APPLY, 0x00, 0x02);
  mb_tx_done();
}

static void test_exceptions(void)
{
  const uint8_t fc1[] = {0x01, 0x01, 0x00, 0x00, 0x00, 0x01};
  const uint8_t pastEnd[] = {0x01, 0x03, HI(MB_HOLD_LAST + 1), LO(MB_HOLD_LAST + 1), 0x00, 0x01};
  const uint8_t spanEnd[] = {0x01, 0x03, HI(MB_HOLD_LAST), LO(MB_HOLD_LAST), 0x00, 0x02};
  const uint8_t count0[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x00};
  const uint8_t count126[] = {0x01, 0x04, 0x00, 0x00, 0x00, 0x7E};
  const uint8_t badBytes[] = {0x01, 0x10, 0x00, 0x08, 0x00, 0x01, 0x04, 0x00, 0x01, 0x00, 0x02};
  const uint8_t single[] = {0x01, 0x06, HI(MB_HOLD_LAST + 1), LO(MB_HOLD_LAST + 1), 0x00, 0x01};

  mb_setup(0);
  mb_request(fc1, sizeof(fc1));
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x81, 0x01);
  CHECK(mb_crc_ok());
  mb_tx_done();
  mb_request(pastEnd, sizeof(pastEnd));
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x83, 0x02);
  mb_tx_done();
  mb_request(spanEnd, sizeof(spanEnd));
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x83, 0x02);
  mb_tx_done();
  mb_request(count0, sizeof(count0));
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x83, 0x03);
  mb_tx_done();
  mb_request(count126, sizeof(count126));
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x84, 0x03);
  mb_tx_done();
  mb_request(badBytes, sizeof(badBytes));
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x90, 0x03);
  mb_tx_done();
  mb_request(single, sizeof(single));
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x86, 0x02);
  mb_tx_done();
  CHECK(fake_mb_tx_count == 7);
}

static void test_broadcast_and_other_slave(void)
{
  const uint8_t broadcast[] = {0x00, 0x06, 0x00, MB_HOLD_COM_MB_TIMEOUT, 0x00, 0x07};
  const uint8_t other[] = {0x02, 0x06, 0x00, MB_HOLD_COM_MB_TIMEOUT, 0x00, 0x09};

  mb_setup(0);
  mb_request(broadcast, sizeof(broadcast));
  CHECK(conf.com.mb_timeout == 7);
  CHECK(fake_mb_tx_count == 0);
  mb_request(other, sizeof(other));
  CHECK(conf.com.mb_timeout == 7);
  CHECK(fake_mb_tx_count == 0);
}

static void test_bad_crc_is_dropped(void)
{
  uint8_t frame[8] = {0x01, 0x06, 0x00, MB_HOLD_COM_MB_TIMEOUT, 0x00, 0x03};
  const uint8_t good[] = {0x01, 0x03, 0x00, MB_HOLD_COM_MB_TIMEOUT, 0x00, 0x01};
  uint32_t errors;

  mb_setup(0);
  conf.com.mb_timeout = 1;
  mb_add_crc(frame, 6);
  frame[7] ^= 0xFF;
  errors = MbSlave_GetErrorCount();
  MbSlave_RxFrame(frame, sizeof(frame));
  MbSlave_Handle();
  fake_tick += 10;
  MbSlave_Handle();
  CHECK(fake_mb_tx_count == 0);
  CHECK(conf.com.mb_timeout == 1);
  CHECK(MbSlave_GetErrorCount() == errors + 1);
  mb_request(good, sizeof(good));
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x03, 0x02, 0x00, 0x01);
  mb_tx_done();
}

static void test_split_frame(void)
{
  uint8_t frame[8] = {0x01, 0x03, 0x00, MB_HOLD_COM_MB_TIMEOUT, 0x00, 0x01};

  mb_setup(0);
  mb_add_crc(frame, 6);
  MbSlave_RxFrame(frame, 3);
  MbSlave_Handle();
  CHECK(fake_mb_tx_count == 0);
  fake_tick += 1;
  MbSlave_RxFrame(frame + 3, 5);
  MbSlave_Handle();
  CHECK(fake_mb_tx_count == 1);
  mb_tx_done();
}

static void test_stale_partial_frame(void)
{
  const uint8_t req[] = {0x01, 0x03, 0x00, MB_HOLD_COM_MB_TIMEOUT, 0x00, 0x01};

  mb_setup(0);
  MbSlave_RxFrame(req, 3);
  MbSlave_Handle();
  fake_tick += 10; /* longer than t3.5 at 19200 Bd */
  MbSlave_Handle();
  mb_request(req, sizeof(req));
  CHECK(fake_mb_tx_count == 1);
  CHECK(fake_mb_tx[1] == 0x03 && fake_mb_tx[2] == 0x02);
  mb_tx_done();
}

static void test_rx_during_tx_is_dropped(void)
{
  uint8_t frame[8] = {0x01, 0x03, 0x00, MB_HOLD_COM_MB_TIMEOUT, 0x00, 0x01};

  mb_setup(0);
  mb_request(frame, 6);
  CHECK(fake_mb_tx_count == 1);
  MbSlave_RxFrame(frame, mb_add_crc(frame, 6)); /* still transmitting */
  MbSlave_Handle();
  mb_tx_done();
  CHECK(fake_mb_tx_count == 1);
}

static void test_communication_timeout(void)
{
  const uint8_t req[] = {0x01, 0x03, 0x00, MB_HOLD_COM_MB_TIMEOUT, 0x00, 0x01};

  mb_setup(1); /* 1 s */
  fake_tick += 999;
  MbSlave_Handle();
  CHECK(!MbSlave_IsTimeout());
  fake_tick += 1;
  MbSlave_Handle();
  CHECK(MbSlave_IsTimeout());
  CHECK((conf.sys.status & STAT_BIT_MB_TIMEOUT) != 0);
  mb_request(req, sizeof(req));
  CHECK(!MbSlave_IsTimeout());
  CHECK((conf.sys.status & STAT_BIT_MB_TIMEOUT) == 0);
  mb_tx_done();
}

static void test_reconfigure_after_response(void)
{
  const uint8_t apply[] = {0x01, 0x06, 0x00, MB_HOLD_COM_MB_APPLY, 0x00, 0x01};

  mb_setup(0);
  conf.com.mb_baud_rate = MB_BAUD_115200;
  mb_request(apply, sizeof(apply));
  CHECK(fake_mb_tx_count == 1);
  CHECK(fake_mb_init_count == 1); /* response still goes out at 19200 Bd */
  CHECK(conf.com.mb_apply == 0);
  mb_tx_done();
  CHECK(fake_mb_init_count == 2);
  CHECK(fake_mb_cfg.baudRate == 115200);
}

static void test_address_saturation(void)
{
  MbSlave_Config_t cfg = {0, 19200, MBSLAVE_PARITY_EVEN, 1, 0};

  MbSlave_Init(&cfg);
  CHECK(fake_mb_cfg.address == 1);
  cfg.address = 250;
  MbSlave_Init(&cfg);
  CHECK(fake_mb_cfg.address == 247);
}

void test_modbus(void)
{
  test_crc_known_vector();
  test_read_holding();
  test_read_input_two_words();
  test_write_single();
  test_write_multiple();
  test_exceptions();
  test_broadcast_and_other_slave();
  test_bad_crc_is_dropped();
  test_split_frame();
  test_stale_partial_frame();
  test_rx_during_tx_is_dropped();
  test_communication_timeout();
  test_reconfigure_after_response();
  test_address_saturation();
}
```

- [ ] **Step 3: Run to verify it fails**

Run: `pytest tests/test_firmware_core.py -q`
Expected: FAIL — `modbus_slave.h: No such file or directory`

- [ ] **Step 4: Add the Modbus section to `core/port.h`** (before `#endif /* PORT_H_ */`)

```c
/* Modbus RTU slave ----------------------------------------------------------*/

#if REGMAP_LIB_MODBUS
#include "modbus_slave.h"

/** (Re)configure the UART and start reception (ends any running transfer) */
Status_t MbPort_Init(const MbSlave_Config_t *cfg);

/** Send a response; MbSlave_TxDone must be called when the last bit left the line */
Status_t MbPort_Send(const uint8_t *data, uint16_t len);

/** ISR: a chunk was received; wake the task that calls MbSlave_Handle (may be empty) */
void MbPort_NotifyFromIsr(void);
#endif
```

- [ ] **Step 5: Create `core/modbus_slave.h`**

```c
/**
 * @file       modbus_slave.h
 * @brief      Modbus RTU slave
 * @regmap-lib
 *
 * @defgroup grMbSlave Modbus RTU slave
 * @{
 * @brief Modbus RTU slave on top of the generated mb_rtu_app callbacks
 *
 * @par Main features:
 * - Function codes 3, 4, 6 and 16; others answered with exception 01
 * - Frames delimited by the port (UART idle line); a frame failing the CRC waits up to t3.5
 *   for the rest
 * - Communication timeout reported through MbSlave_TimeoutChanged
 * - Settings changed while running are applied after the current response
 *
 * @par Example
 * @code
 * MbSlave_Config_t cfg;
 * ConfigApp_ModbusConfig(&cfg);
 * MbSlave_Init(&cfg);
 * while (1) { MbSlave_Handle(); }
 * @endcode
 */
#ifndef MODBUS_SLAVE_H_
#define MODBUS_SLAVE_H_

/* Includes ------------------------------------------------------------------*/

#include "common.h"
#include "regmap_lib_conf.h"

/* Definitions----------------------------------------------------------------*/

/** Largest RTU frame: address, PDU (253 bytes) and CRC */
#define MBSLAVE_FRAME_MAX       256u

/* Typedefs-------------------------------------------------------------------*/

typedef enum
{
  MBSLAVE_PARITY_NONE = 0,
  MBSLAVE_PARITY_EVEN = 1,
  MBSLAVE_PARITY_ODD = 2,
} MbSlave_Parity_t;

typedef struct
{
  uint8_t address;      ///< Slave address, saturated to 1..247
  uint32_t baudRate;    ///< Bit/s
  uint8_t parity;       ///< MbSlave_Parity_t
  uint8_t stopBits;     ///< 1 or 2
  uint32_t timeoutMs;   ///< Communication timeout, 0 = disabled
} MbSlave_Config_t;

/* Functions -----------------------------------------------------------------*/

/** Initialise the slave and the port with the given settings */
Status_t MbSlave_Init(const MbSlave_Config_t *cfg);

/** Request new settings; applied by MbSlave_Handle when no response is being sent */
Status_t MbSlave_Reconfigure(const MbSlave_Config_t *cfg);

/** Process a received frame, send the response, handle timeout; call periodically */
Status_t MbSlave_Handle(void);

/** True while no valid frame arrived for the configured timeout */
bool MbSlave_IsTimeout(void);

/** Number of dropped frames and chunks since MbSlave_Init */
uint32_t MbSlave_GetErrorCount(void);

/** Modbus CRC16 of data; the low byte is transmitted first */
uint16_t MbSlave_Crc16(const uint8_t *data, uint16_t len);

/** ISR (port): a chunk of a frame was received */
void MbSlave_RxFrame(const uint8_t *data, uint16_t len);

/** ISR (port): the response has been transmitted completely */
void MbSlave_TxDone(void);

/** Application hook (config_app.c): the communication timeout state changed */
void MbSlave_TimeoutChanged(bool timeout);

#endif /* MODBUS_SLAVE_H_ */
/** @} */
```

- [ ] **Step 6: Create `core/modbus_slave.c`**

```c
/**
 * @file       modbus_slave.c
 * @brief      Modbus RTU slave
 * @regmap-lib
 * @addtogroup grMbSlave
 * @{
 */

/* Includes ------------------------------------------------------------------*/

#include "modbus_slave.h"

#if REGMAP_LIB_MODBUS

#include "lib_bytes.h"
#include "mb_rtu_app.h"
#include "port.h"
/* [upgrade include] */

/* Private defines -----------------------------------------------------------*/

#define MB_BROADCAST            0
#define MB_MIN_SLAVE_ADDR       1
#define MB_MAX_SLAVE_ADDR       247
#define MB_EXCEPTION_MASK       0x80
#define MB_FC_READ_HOLDING      3
#define MB_FC_READ_INPUT        4
#define MB_FC_WRITE_SINGLE      6
#define MB_FC_WRITE_MULTIPLE    16
#define MB_EX_ILLEGAL_FUNCTION  1
#define MB_EX_ILLEGAL_ADDRESS   2
#define MB_EX_ILLEGAL_VALUE     3
#define MB_MAX_READ_COUNT       125
#define MB_MAX_WRITE_COUNT      123
#define MB_MIN_GAP_MS           2u
#define MB_DEFAULT_BAUD_RATE    19200u

/* Private typedefs ----------------------------------------------------------*/

typedef struct
{
  MbSlave_Config_t cfg;           ///< Active settings
  MbSlave_Config_t pending;       ///< Settings waiting for the end of the response
  bool reconfigure;               ///< pending is valid
  uint8_t rx[MBSLAVE_FRAME_MAX];  ///< Received chunks of the current frame
  volatile uint16_t rxLen;        ///< Bytes in rx (ISR appends, Handle resets)
  volatile uint32_t rxTick;       ///< Tick of the last received chunk
  volatile bool txBusy;           ///< Response handed to the port, not finished yet
  uint8_t tx[MBSLAVE_FRAME_MAX];  ///< Response frame
  uint32_t gapMs;                 ///< t3.5 rounded up, at least MB_MIN_GAP_MS
  uint32_t lastFrameTick;         ///< Tick of the last valid frame for this slave
  bool timeout;                   ///< Communication timeout active
  volatile uint32_t errors;       ///< Dropped frames and chunks
} MbSlave_Private_t;

/* Private variables ---------------------------------------------------------*/

static MbSlave_Private_t mb;

/* Private function prototypes -----------------------------------------------*/

static void MbSlave_Apply(const MbSlave_Config_t *cfg);
static void MbSlave_ClearRx(void);
static uint16_t MbSlave_Process(const uint8_t *req, uint16_t len, uint8_t *resp);
static uint16_t MbSlave_Exception(uint8_t *resp, uint8_t fc, uint8_t code);
static bool MbSlave_InRange(uint16_t addr, uint16_t count, int32_t last);

/* Functions -----------------------------------------------------------------*/

Status_t MbSlave_Init(const MbSlave_Config_t *cfg)
{
  memset(&mb, 0, sizeof(mb));
  MbSlave_Apply(cfg);
  mb.lastFrameTick = Port_GetTickMs();
  /* [upgrade init] */

  return STATUS_OK;
}


Status_t MbSlave_Reconfigure(const MbSlave_Config_t *cfg)
{
  mb.pending = *cfg;
  mb.reconfigure = true;

  return STATUS_OK;
}


Status_t MbSlave_Handle(void)
{
  uint32_t now = Port_GetTickMs();
  uint32_t rxTick;
  uint16_t len;
  uint16_t respLen;

  Port_CriticalEnter();
  len = mb.rxLen;
  rxTick = mb.rxTick;
  Port_CriticalExit();

  if (len > 0 && !mb.txBusy)
  {
    if (len >= 4 && MbSlave_Crc16(mb.rx, (uint16_t)(len - 2)) == Bytes_GetU16Le(mb.rx + len - 2))
    {
      if (mb.rx[0] == mb.cfg.address || mb.rx[0] == MB_BROADCAST)
      {
        respLen = MbSlave_Process(mb.rx, (uint16_t)(len - 2), mb.tx);
        mb.lastFrameTick = now;

        if (mb.rx[0] != MB_BROADCAST)
        {
          Bytes_PutU16Le(mb.tx + respLen, MbSlave_Crc16(mb.tx, respLen));
          mb.txBusy = true;
          if (MbPort_Send(mb.tx, (uint16_t)(respLen + 2)) != STATUS_OK)
          {
            mb.txBusy = false;
            mb.errors++;
          }
        }
      }
      MbSlave_ClearRx();
    }
    else if (now - rxTick >= mb.gapMs)
    {
      /* Incomplete or corrupted frame */
      MbSlave_ClearRx();
      mb.errors++;
    }
  }

  if (mb.reconfigure && !mb.txBusy)
  {
    mb.reconfigure = false;
    MbSlave_Apply(&mb.pending);
  }

  if (!mb.timeout && mb.cfg.timeoutMs != 0 && now - mb.lastFrameTick >= mb.cfg.timeoutMs)
  {
    mb.timeout = true;
    MbSlave_TimeoutChanged(true);
  }
  else if (mb.timeout && (mb.cfg.timeoutMs == 0 || now - mb.lastFrameTick < mb.cfg.timeoutMs))
  {
    mb.timeout = false;
    MbSlave_TimeoutChanged(false);
  }

  /* [upgrade handle] */

  return STATUS_OK;
}


bool MbSlave_IsTimeout(void)
{
  return mb.timeout;
}


uint32_t MbSlave_GetErrorCount(void)
{
  return mb.errors;
}


uint16_t MbSlave_Crc16(const uint8_t *data, uint16_t len)
{
  uint16_t crc = 0xFFFFu;
  uint16_t i;
  uint8_t bit;

  for (i = 0; i < len; i++)
  {
    crc ^= data[i];
    for (bit = 0; bit < 8; bit++)
    {
      crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xA001u) : (uint16_t)(crc >> 1);
    }
  }

  return crc;
}


void MbSlave_RxFrame(const uint8_t *data, uint16_t len)
{
  uint16_t rxLen = mb.rxLen;

  if (mb.txBusy)
  {
    return;
  }
  if (len > MBSLAVE_FRAME_MAX - rxLen)
  {
    mb.rxLen = 0;
    mb.errors++;
    return;
  }

  memcpy(mb.rx + rxLen, data, len);
  mb.rxTick = Port_GetTickMs();
  mb.rxLen = (uint16_t)(rxLen + len);
  MbPort_NotifyFromIsr();
}


void MbSlave_TxDone(void)
{
  mb.txBusy = false;
}

/* Private Functions ---------------------------------------------------------*/

static void MbSlave_Apply(const MbSlave_Config_t *cfg)
{
  mb.cfg = *cfg;
  SAT_DOWN(mb.cfg.address, MB_MIN_SLAVE_ADDR);
  SAT_UP(mb.cfg.address, MB_MAX_SLAVE_ADDR);
  if (mb.cfg.baudRate == 0)
  {
    mb.cfg.baudRate = MB_DEFAULT_BAUD_RATE;
  }

  /* t3.5 = 3.5 characters of 11 bits, rounded up, +1 ms for the tick resolution */
  mb.gapMs = (38500u + mb.cfg.baudRate - 1u) / mb.cfg.baudRate + 1u;
  SAT_DOWN(mb.gapMs, MB_MIN_GAP_MS);

  MbSlave_ClearRx();
  (void)MbPort_Init(&mb.cfg);
}


static void MbSlave_ClearRx(void)
{
  Port_CriticalEnter();
  mb.rxLen = 0;
  Port_CriticalExit();
}


static uint16_t MbSlave_Exception(uint8_t *resp, uint8_t fc, uint8_t code)
{
  resp[1] = (uint8_t)(fc | MB_EXCEPTION_MASK);
  resp[2] = code;

  return 3;
}


static bool MbSlave_InRange(uint16_t addr, uint16_t count, int32_t last)
{
  return (int32_t)addr + (int32_t)count - 1 <= last;
}

/* [upgrade range] */

/**
 * Process a request (without CRC) and build the response (without CRC).
 * @return Length of the response
 */
static uint16_t MbSlave_Process(const uint8_t *req, uint16_t len, uint8_t *resp)
{
  uint8_t fc = req[1];
  uint16_t addr;
  uint16_t count;
  uint16_t value;
  uint16_t i;

  resp[0] = mb.cfg.address;
  resp[1] = fc;

  if (fc != MB_FC_READ_HOLDING && fc != MB_FC_READ_INPUT && fc != MB_FC_WRITE_SINGLE
      && fc != MB_FC_WRITE_MULTIPLE)
  {
    return MbSlave_Exception(resp, fc, MB_EX_ILLEGAL_FUNCTION);
  }
  if (len < 6)
  {
    return MbSlave_Exception(resp, fc, MB_EX_ILLEGAL_VALUE);
  }

  addr = Bytes_GetU16Be(req + 2);
  count = Bytes_GetU16Be(req + 4); /* FC 6: the value */

  if (fc == MB_FC_READ_HOLDING || fc == MB_FC_READ_INPUT)
  {
    if (len != 6 || count < 1 || count > MB_MAX_READ_COUNT)
    {
      return MbSlave_Exception(resp, fc, MB_EX_ILLEGAL_VALUE);
    }
    /* [upgrade read] */
    {
      if (!MbSlave_InRange(addr, count, fc == MB_FC_READ_HOLDING ? MB_HOLD_LAST : MB_INPUT_LAST))
      {
        return MbSlave_Exception(resp, fc, MB_EX_ILLEGAL_ADDRESS);
      }
      for (i = 0; i < count; i++)
      {
        if (fc == MB_FC_READ_HOLDING)
        {
          (void)MbRtu_ReadHoldingRegCallback((uint16_t)(addr + i), &value);
        }
        else
        {
          (void)MbRtu_ReadInputRegCallback((uint16_t)(addr + i), &value);
        }
        /* The callbacks return the value already byte-swapped for the wire */
        memcpy(resp + 3 + 2 * i, &value, 2);
      }
    }
    resp[2] = (uint8_t)(count * 2);
    return (uint16_t)(3 + count * 2);
  }

  if (fc == MB_FC_WRITE_SINGLE)
  {
    if (len != 6)
    {
      return MbSlave_Exception(resp, fc, MB_EX_ILLEGAL_VALUE);
    }
    /* [upgrade write single] */
    {
      if (!MbSlave_InRange(addr, 1, MB_HOLD_LAST))
      {
        return MbSlave_Exception(resp, fc, MB_EX_ILLEGAL_ADDRESS);
      }
      (void)MbRtu_WriteHoldingRegCallback(addr, count);
    }
    memcpy(resp + 2, req + 2, 4);
    return 6;
  }

  /* MB_FC_WRITE_MULTIPLE */
  if (count < 1 || count > MB_MAX_WRITE_COUNT || len < 7 || req[6] != count * 2
      || len != 7 + count * 2)
  {
    return MbSlave_Exception(resp, fc, MB_EX_ILLEGAL_VALUE);
  }
  /* [upgrade write multiple] */
  {
    if (!MbSlave_InRange(addr, count, MB_HOLD_LAST))
    {
      return MbSlave_Exception(resp, fc, MB_EX_ILLEGAL_ADDRESS);
    }
    for (i = 0; i < count; i++)
    {
      (void)MbRtu_WriteHoldingRegCallback((uint16_t)(addr + i), Bytes_GetU16Be(req + 7 + 2 * i));
    }
  }
  memcpy(resp + 2, req + 2, 4);
  return 6;
}

#endif /* REGMAP_LIB_MODBUS */

/** @} */
```

The `/* [upgrade ...] */` comments are anchors that Task 5 replaces with `#if REGMAP_LIB_UPGRADE` code; leave them in place.

- [ ] **Step 7: Add the Modbus mapping to the port `config_app.h/.c`**

In `config_app.h`, add before `#endif /* CONFIG_APP_H_ */`:

```c
#if REGMAP_LIB_MODBUS
#include "modbus_slave.h"

/**
 * Build the Modbus settings from the COM registers (MB_BAUD_RATE, MB_PARITY, MB_STOP_BITS,
 * MB_ADDRESS, MB_TIMEOUT of the `regmap init` starter map). Adapt when the map differs.
 * @param cfg Settings to fill
 */
void ConfigApp_ModbusConfig(MbSlave_Config_t *cfg);
#endif
```

Replace the whole `config_app.c` with:

```c
/**
 * @file       config_app.c
 * @brief      Application hooks of the configuration module
 *
 * Port file: written once by `regmap export-lib`, then owned by the project. The Modbus part
 * assumes the COM registers and STAT_BIT_MB_TIMEOUT of the `regmap init` starter map.
 */

#include "config_app.h"
#include "configuration.h"

#if REGMAP_LIB_MODBUS
/** Baud rates selected by the MB_BAUD_RATE enumeration */
static const uint32_t baudRates[] = {9600, 19200, 38400, 57600, 115200};
#endif

Status_t Config_AppInit(void)
{
  /* Restore stored registers here, e.g. Config_ReadStream() on the flash copy */
  return STATUS_OK;
}


Status_t Config_Callback(uint32_t id)
{
#if REGMAP_LIB_MODBUS
  MbSlave_Config_t cfg;

  /* Writing 1 to MB_APPLY applies the Modbus settings after the current response */
  if (id == CONF_COM_MB_APPLY && conf.com.mb_apply != 0)
  {
    conf.com.mb_apply = 0;
    ConfigApp_ModbusConfig(&cfg);
    (void)MbSlave_Reconfigure(&cfg);
  }
#else
  UNUSED(id);
#endif

  /* Store flash registers here: (id & 0x070) == 0x070 */
  return STATUS_OK;
}

#if REGMAP_LIB_MODBUS

void ConfigApp_ModbusConfig(MbSlave_Config_t *cfg)
{
  uint32_t baud = (uint32_t)conf.com.mb_baud_rate;

  /* Enumeration index, or the baud rate itself when the register holds a number */
  cfg->baudRate = (baud < sizeof(baudRates) / sizeof(baudRates[0])) ? baudRates[baud] : baud;
  cfg->parity = (uint8_t)conf.com.mb_parity;
  cfg->stopBits = (conf.com.mb_stop_bits == MB_STOP_2) ? 2u : 1u;
  cfg->address = (uint8_t)conf.com.mb_address;
  cfg->timeoutMs = (uint32_t)conf.com.mb_timeout * 1000u;
}


void MbSlave_TimeoutChanged(bool timeout)
{
  if (timeout)
  {
    conf.sys.status |= STAT_BIT_MB_TIMEOUT;
  }
  else
  {
    conf.sys.status &= ~(uint32_t)STAT_BIT_MB_TIMEOUT;
  }
}

#endif /* REGMAP_LIB_MODBUS */
```

- [ ] **Step 8: Run tests to verify they pass**

Run: `pytest tests/test_firmware_core.py -q`
Expected: PASS

- [ ] **Step 9: Commit**

```bash
git add src/regmap/firmware_lib tests/c
git commit -m "feat: Modbus RTU slave core of the firmware library"
```

---

### Task 4: LeBin protocol core

**Files:**
- Create: `src/regmap/firmware_lib/core/lebin.h`
- Create: `src/regmap/firmware_lib/core/lebin.c`
- Modify: `src/regmap/firmware_lib/core/port.h` (LeBin section)
- Modify: `tests/c/fake_port.h`, `tests/c/fake_port.c`, `tests/c/test_main.c`
- Create: `tests/c/test_lebin.c`

**Interfaces:**
- Consumes: `configuration.h` (`Config_CheckLimits`, `Config_ApplyConfig`, `CONF_PTR`, `CONF_BYTE_LEN_ID`), `lib_bytes.h`, `Port_*`.
- Produces: `LEBIN_START_BYTE` (0x90), `LEBIN_HEADER_LEN` (4), `LEBIN_MIN_LENGTH` (8), `Lebin_PacketId_t` (`LEBIN_ERROR=1, LEBIN_READ_REG_RESP=5, LEBIN_TIME_SER=8, LEBIN_FW_UPG_ACK=127, LEBIN_WRITE_REG=129, LEBIN_READ_REG=130, LEBIN_FW_UPGRADE=255`), `Status_t Lebin_Init(void)`, `Status_t Lebin_Handle(void)`, `void Lebin_RxBytes(const uint8_t *data, uint32_t len)` (ISR), `uint16_t Lebin_ProcessPacket(const uint8_t *req, uint8_t *resp)`, `Status_t Lebin_FillSeries(uint8_t *buffer, uint32_t id, uint32_t timestamp, uint32_t delta, uint32_t count)`, `uint32_t Lebin_GetErrorCount(void)`; port: `Status_t LebinPort_Init(void)`, `bool LebinPort_TxReady(void)`, `Status_t LebinPort_Send(const uint8_t *data, uint16_t len)`, `void LebinPort_NotifyFromIsr(void)`.
- Task 5 replaces the `/* [upgrade ...] */` anchors in `lebin.c`.

- [ ] **Step 1: Extend the fake port**

Append to `tests/c/fake_port.h` before `#endif`:

```c
/* LeBin */
#include "lebin.h"

extern bool fake_lebin_busy;              /* LebinPort_TxReady returns !fake_lebin_busy */
extern uint8_t fake_lebin_tx[LEBIN_PACKET_MAX];
extern uint16_t fake_lebin_tx_len;
extern int fake_lebin_tx_count;
extern int fake_lebin_init_count;

void fake_lebin_reset(void);
```

Append to `tests/c/fake_port.c`:

```c
/* LeBin ---------------------------------------------------------------------*/

bool fake_lebin_busy;
uint8_t fake_lebin_tx[LEBIN_PACKET_MAX];
uint16_t fake_lebin_tx_len;
int fake_lebin_tx_count;
int fake_lebin_init_count;

Status_t LebinPort_Init(void)
{
  fake_lebin_init_count++;
  return STATUS_OK;
}

bool LebinPort_TxReady(void)
{
  return !fake_lebin_busy;
}

Status_t LebinPort_Send(const uint8_t *data, uint16_t len)
{
  if (fake_lebin_busy)
  {
    return STATUS_BUSY;
  }
  memcpy(fake_lebin_tx, data, len);
  fake_lebin_tx_len = len;
  fake_lebin_tx_count++;
  return STATUS_OK;
}

void LebinPort_NotifyFromIsr(void)
{
}

void fake_lebin_reset(void)
{
  fake_lebin_busy = false;
  fake_lebin_tx_len = 0;
  fake_lebin_tx_count = 0;
  fake_lebin_init_count = 0;
}
```

In `tests/c/test_main.c` declare `void test_lebin(void);` and call it after `test_modbus();`.

- [ ] **Step 2: Write the failing tests `tests/c/test_lebin.c`**

```c
#include "check.h"
#include "configuration.h"
#include "fake_port.h"
#include "lebin.h"
#include "lib_bytes.h"

#define INVALID_ID  0x7F000012u   /* block beyond the map, length code 2 (4 bytes) */

static void lb_setup(void)
{
  fake_tick = 1000;
  fake_lebin_reset();
  Config_Init();
  Lebin_Init();
}

/* Build a packet in buf: header + payload; returns its length. */
static uint16_t lb_packet(uint8_t *buf, uint8_t id, const uint8_t *payload, uint16_t len)
{
  buf[0] = LEBIN_START_BYTE;
  buf[1] = id;
  Bytes_PutU16Le(buf + 2, (uint16_t)(len + LEBIN_HEADER_LEN));
  memcpy(buf + LEBIN_HEADER_LEN, payload, len);
  return (uint16_t)(len + LEBIN_HEADER_LEN);
}

/* Read request for SYS_UPTIME and FACT_SERIAL_NUMBER (20-byte response). */
static uint16_t lb_read_request(uint8_t *buf)
{
  uint8_t payload[8];

  Bytes_PutU32Le(payload, CONF_SYS_UPTIME);
  Bytes_PutU32Le(payload + 4, CONF_FACT_SERIAL_NUMBER);
  return lb_packet(buf, LEBIN_READ_REG, payload, sizeof(payload));
}

static void test_init_starts_port(void)
{
  lb_setup();
  CHECK(fake_lebin_init_count == 1);
}

static void test_read_registers(void)
{
  uint8_t pkt[32];
  uint16_t n;

  lb_setup();
  conf.sys.uptime = 0x11223344;
  conf.fact.serial_number = 0xAABBCCDD;
  n = lb_read_request(pkt);
  Lebin_RxBytes(pkt, n);
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 1);
  CHECK_BYTES(fake_lebin_tx, fake_lebin_tx_len,
              0x90, 0x05, 0x14, 0x00,
              0x12, 0x01, 0x00, 0x00, 0x44, 0x33, 0x22, 0x11,
              0x12, 0x01, 0x00, 0x01, 0xDD, 0xCC, 0xBB, 0xAA);
}

static void test_read_skips_invalid_id(void)
{
  uint8_t payload[8];
  uint8_t pkt[32];
  uint16_t n;

  lb_setup();
  conf.sys.uptime = 0x11223344;
  Bytes_PutU32Le(payload, INVALID_ID);
  Bytes_PutU32Le(payload + 4, CONF_SYS_UPTIME);
  n = lb_packet(pkt, LEBIN_READ_REG, payload, sizeof(payload));
  Lebin_RxBytes(pkt, n);
  Lebin_Handle();
  CHECK_BYTES(fake_lebin_tx, fake_lebin_tx_len,
              0x90, 0x05, 0x0C, 0x00, 0x12, 0x01, 0x00, 0x00, 0x44, 0x33, 0x22, 0x11);
}

static void test_write_register(void)
{
  uint8_t payload[6];
  uint8_t pkt[32];
  uint16_t n;

  lb_setup();
  Bytes_PutU32Le(payload, CONF_COM_MB_TIMEOUT);
  Bytes_PutU16Le(payload + 4, 10);
  n = lb_packet(pkt, LEBIN_WRITE_REG, payload, sizeof(payload));
  Lebin_RxBytes(pkt, n);
  Lebin_Handle();
  CHECK(conf.com.mb_timeout == 10);
  CHECK_BYTES(fake_lebin_tx, fake_lebin_tx_len,
              0x90, 0x05, 0x0A, 0x00, 0x71, 0x81, 0x00, 0x03, 0x0A, 0x00);
}

static void test_write_invalid_id_then_valid(void)
{
  uint8_t payload[14];
  uint8_t pkt[32];
  uint16_t n;

  lb_setup();
  Bytes_PutU32Le(payload, INVALID_ID);
  Bytes_PutU32Le(payload + 4, 0x01020304u);
  Bytes_PutU32Le(payload + 8, CONF_COM_MB_TIMEOUT);
  Bytes_PutU16Le(payload + 12, 11);
  n = lb_packet(pkt, LEBIN_WRITE_REG, payload, sizeof(payload));
  Lebin_RxBytes(pkt, n);
  Lebin_Handle();
  CHECK(conf.com.mb_timeout == 11);
  CHECK_BYTES(fake_lebin_tx, fake_lebin_tx_len,
              0x90, 0x05, 0x12, 0x00,
              0x12, 0x00, 0x00, 0x7F, 0xF0, 0xF0, 0xF0, 0xF0,
              0x71, 0x81, 0x00, 0x03, 0x0B, 0x00);
}

static void test_packet_in_chunks(void)
{
  uint8_t pkt[32];
  uint16_t n;

  lb_setup();
  n = lb_read_request(pkt);
  Lebin_RxBytes(pkt, 3);
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 0);
  Lebin_RxBytes(pkt + 3, (uint32_t)(n - 3));
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 1);
  CHECK(fake_lebin_tx_len == 20);
}

static void test_two_packets_in_one_chunk(void)
{
  uint8_t pkt[64];
  uint16_t n;

  lb_setup();
  n = lb_read_request(pkt);
  n = (uint16_t)(n + lb_read_request(pkt + n));
  Lebin_RxBytes(pkt, n);
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 1); /* one response per call */
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 2);
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 2);
}

static void test_waits_while_port_busy(void)
{
  uint8_t pkt[32];
  uint16_t n;

  lb_setup();
  fake_lebin_busy = true;
  n = lb_read_request(pkt);
  Lebin_RxBytes(pkt, n);
  CHECK(Lebin_Handle() == STATUS_BUSY);
  CHECK(fake_lebin_tx_count == 0);
  fake_lebin_busy = false;
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 1);
}

static void test_resync_after_garbage(void)
{
  uint8_t pkt[32] = {0x00, 0x11};
  uint16_t n;
  uint32_t errors;

  lb_setup();
  errors = Lebin_GetErrorCount();
  n = lb_read_request(pkt + 2);
  Lebin_RxBytes(pkt, (uint32_t)(n + 2));
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 1);
  CHECK(Lebin_GetErrorCount() > errors);
}

static void test_resync_after_bad_length(void)
{
  uint8_t pkt[32] = {0x90, 0x82, 0xFF, 0xFF};
  uint16_t n;

  lb_setup();
  n = lb_read_request(pkt + 4);
  Lebin_RxBytes(pkt, (uint32_t)(n + 4));
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 1);
  CHECK(fake_lebin_tx_len == 20);
}

static void test_stale_partial_packet_is_dropped(void)
{
  uint8_t pkt[32];
  uint16_t n;

  lb_setup();
  n = lb_read_request(pkt);
  Lebin_RxBytes(pkt, 6);
  Lebin_Handle();
  fake_tick += LEBIN_NEW_PACKET_MS + 1;
  Lebin_Handle();
  Lebin_RxBytes(pkt, n);
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 1);
  CHECK(fake_lebin_tx_len == 20);
}

static void test_overflow_drops_chunk(void)
{
  static uint8_t big[LEBIN_RX_BUFFER_SIZE + 1];
  uint8_t pkt[32];
  uint16_t n;
  uint32_t errors;

  lb_setup();
  errors = Lebin_GetErrorCount();
  Lebin_RxBytes(big, sizeof(big));
  CHECK(Lebin_GetErrorCount() == errors + 1);
  n = lb_read_request(pkt);
  Lebin_RxBytes(pkt, n);
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 1);
}

static void test_unknown_packet_has_no_response(void)
{
  const uint8_t payload[4] = {1, 2, 3, 4};
  uint8_t pkt[16];
  uint16_t n;

  lb_setup();
  n = lb_packet(pkt, 0x42, payload, sizeof(payload));
  Lebin_RxBytes(pkt, n);
  Lebin_Handle();
  CHECK(fake_lebin_tx_count == 0);
}

static void test_fill_series(void)
{
  uint8_t buf[20];

  CHECK(Lebin_FillSeries(buf, CONF_SYS_UPTIME, 1, 2, 3) == STATUS_OK);
  CHECK_BYTES(buf, sizeof(buf),
              0x90, 0x08, 0x20, 0x00,
              0x12, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
              0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00);
}

void test_lebin(void)
{
  test_init_starts_port();
  test_read_registers();
  test_read_skips_invalid_id();
  test_write_register();
  test_write_invalid_id_then_valid();
  test_packet_in_chunks();
  test_two_packets_in_one_chunk();
  test_waits_while_port_busy();
  test_resync_after_garbage();
  test_resync_after_bad_length();
  test_stale_partial_packet_is_dropped();
  test_overflow_drops_chunk();
  test_unknown_packet_has_no_response();
  test_fill_series();
}
```

- [ ] **Step 3: Run to verify it fails**

Run: `pytest tests/test_firmware_core.py -q`
Expected: FAIL — `lebin.h: No such file or directory`

- [ ] **Step 4: Add the LeBin section to `core/port.h`** (before `#endif /* PORT_H_ */`)

```c
/* LeBin ---------------------------------------------------------------------*/

#if REGMAP_LIB_LEBIN
/** Start reception (UART transport); nothing to do for USB CDC */
Status_t LebinPort_Init(void);

/** True when the previous response has been handed over and the buffer may be reused */
bool LebinPort_TxReady(void);

/** Send a response; data stays valid until LebinPort_TxReady returns true again */
Status_t LebinPort_Send(const uint8_t *data, uint16_t len);

/** ISR: bytes were received; wake the task that calls Lebin_Handle (may be empty) */
void LebinPort_NotifyFromIsr(void);
#endif
```

- [ ] **Step 5: Create `core/lebin.h`**

```c
/**
 * @file       lebin.h
 * @brief      LeBin binary protocol
 * @regmap-lib
 *
 * @defgroup grLebin LeBin protocol
 * @{
 * @brief Register read/write and firmware upgrade over a byte stream (USB CDC, UART)
 *
 * Packet: start byte 0x90, packet ID, total length (16 bit LE, header included), payload.
 *
 * @par Example
 * @code
 * Lebin_Init();
 * while (1) { Lebin_Handle(); }
 * @endcode
 */
#ifndef LEBIN_H_
#define LEBIN_H_

/* Includes ------------------------------------------------------------------*/

#include "common.h"
#include "regmap_lib_conf.h"

/* Definitions----------------------------------------------------------------*/

#define LEBIN_START_BYTE        0x90u
#define LEBIN_HEADER_LEN        4u
#define LEBIN_MIN_LENGTH        8u

typedef enum
{
  LEBIN_ERROR          = 1,    ///< Program error code
  LEBIN_READ_REG_RESP  = 5,    ///< Response to read/write register
  LEBIN_TIME_SER       = 8,    ///< Time series
  LEBIN_FW_UPG_ACK     = 127,  ///< Acknowledge of FW upgrade packet
  LEBIN_WRITE_REG      = 129,  ///< Write register request
  LEBIN_READ_REG       = 130,  ///< Read register request
  LEBIN_FW_UPGRADE     = 255,  ///< FW upgrade data
} Lebin_PacketId_t;

/* Functions -----------------------------------------------------------------*/

/** Reset the protocol state and start the port */
Status_t Lebin_Init(void);

/**
 * Assemble received bytes into packets and answer one packet per call; call periodically.
 * @return STATUS_BUSY while a complete packet waits for the port
 */
Status_t Lebin_Handle(void);

/** ISR (port): bytes received; a chunk that does not fit is dropped as a whole */
void Lebin_RxBytes(const uint8_t *data, uint32_t len);

/**
 * Process one complete packet (length >= LEBIN_MIN_LENGTH) and build the response.
 * @return Length of the response, 0 = no response
 */
uint16_t Lebin_ProcessPacket(const uint8_t *req, uint8_t *resp);

/**
 * Fill the header of a time series packet; samples follow at offset 20.
 * @return STATUS_ERROR if the packet would be longer than 65535 bytes
 */
Status_t Lebin_FillSeries(uint8_t *buffer, uint32_t id, uint32_t timestamp, uint32_t delta,
                          uint32_t count);

/** Number of dropped chunks and bytes since Lebin_Init */
uint32_t Lebin_GetErrorCount(void);

#endif /* LEBIN_H_ */
/** @} */
```

- [ ] **Step 6: Create `core/lebin.c`**

```c
/**
 * @file       lebin.c
 * @brief      LeBin binary protocol
 * @regmap-lib
 * @addtogroup grLebin
 * @{
 */

/* Includes ------------------------------------------------------------------*/

#include "lebin.h"

#if REGMAP_LIB_LEBIN

#include "configuration.h"
#include "lib_bytes.h"
#include "port.h"
/* [upgrade include] */

#if (LEBIN_RX_BUFFER_SIZE & (LEBIN_RX_BUFFER_SIZE - 1u)) != 0
#error "LEBIN_RX_BUFFER_SIZE must be a power of two"
#endif

/* Private typedefs ----------------------------------------------------------*/

typedef struct
{
  uint8_t ring[LEBIN_RX_BUFFER_SIZE];   ///< Received bytes (written in ISR)
  volatile uint32_t head;               ///< Written by Lebin_RxBytes only
  volatile uint32_t tail;               ///< Written by Lebin_Handle only
  volatile uint32_t rxTick;             ///< Tick of the last received chunk
  volatile uint32_t errors;             ///< Dropped chunks and bytes
  uint8_t packet[LEBIN_PACKET_MAX];     ///< Packet being assembled
  uint32_t packetLen;                   ///< Bytes in packet
  uint8_t tx[LEBIN_PACKET_MAX];         ///< Response, owned by the port until sent
} Lebin_Private_t;

/* Private variables ---------------------------------------------------------*/

static Lebin_Private_t lb;

/* Private function prototypes -----------------------------------------------*/

static void Lebin_Drop(uint32_t count);
static uint32_t Lebin_ReadReg(const uint8_t *req, uint32_t length, uint8_t *resp);
static uint32_t Lebin_WriteReg(const uint8_t *req, uint32_t length, uint8_t *resp);
/* [upgrade prototype] */

/* Functions -----------------------------------------------------------------*/

Status_t Lebin_Init(void)
{
  memset(&lb, 0, sizeof(lb));

  return LebinPort_Init();
}


void Lebin_RxBytes(const uint8_t *data, uint32_t len)
{
  uint32_t head = lb.head;
  uint32_t i;

  if (len > LEBIN_RX_BUFFER_SIZE - (head - lb.tail))
  {
    lb.errors++;
    return;
  }

  for (i = 0; i < len; i++)
  {
    lb.ring[(head + i) & (LEBIN_RX_BUFFER_SIZE - 1u)] = data[i];
  }
  lb.rxTick = Port_GetTickMs();
  lb.head = head + len;
  LebinPort_NotifyFromIsr();
}


Status_t Lebin_Handle(void)
{
  uint32_t head = lb.head;
  uint32_t length;
  uint16_t respLen;
  const uint8_t *start;

  /* Move received bytes into the packet buffer */
  while (lb.tail != head && lb.packetLen < LEBIN_PACKET_MAX)
  {
    lb.packet[lb.packetLen++] = lb.ring[lb.tail & (LEBIN_RX_BUFFER_SIZE - 1u)];
    lb.tail++;
  }

  while (lb.packetLen > 0)
  {
    if (lb.packet[0] != LEBIN_START_BYTE)
    {
      /* Resynchronise on the next start byte */
      start = memchr(lb.packet + 1, LEBIN_START_BYTE, lb.packetLen - 1);
      Lebin_Drop((start != NULL) ? (uint32_t)(start - lb.packet) : lb.packetLen);
      lb.errors++;
      continue;
    }
    if (lb.packetLen < LEBIN_HEADER_LEN)
    {
      break;
    }
    length = Bytes_GetU16Le(lb.packet + 2);
    if (length < LEBIN_MIN_LENGTH || length > LEBIN_PACKET_MAX)
    {
      /* Not a packet start */
      Lebin_Drop(1);
      lb.errors++;
      continue;
    }
    if (lb.packetLen < length)
    {
      break;
    }
    if (!LebinPort_TxReady())
    {
      return STATUS_BUSY;
    }

    respLen = Lebin_ProcessPacket(lb.packet, lb.tx);
    Lebin_Drop(length);
    if (respLen != 0)
    {
      if (LebinPort_Send(lb.tx, respLen) != STATUS_OK)
      {
        lb.errors++;
      }
      return STATUS_OK;
    }
  }

  /* A partial packet without new bytes for too long is abandoned */
  if (lb.packetLen > 0 && Port_GetTickMs() - lb.rxTick > LEBIN_NEW_PACKET_MS)
  {
    lb.packetLen = 0;
    lb.errors++;
  }

  return STATUS_OK;
}


uint16_t Lebin_ProcessPacket(const uint8_t *req, uint8_t *resp)
{
  uint32_t length = Bytes_GetU16Le(req + 2);
  uint32_t respLen = 0;

  switch (req[1])
  {
    case LEBIN_READ_REG:
      respLen = Lebin_ReadReg(req, length, resp);
      break;

    case LEBIN_WRITE_REG:
      respLen = Lebin_WriteReg(req, length, resp);
      break;

    /* [upgrade case] */

    default:
      /* Unknown packet: no response */
      break;
  }

  if (respLen != 0)
  {
    resp[0] = LEBIN_START_BYTE;
    Bytes_PutU16Le(resp + 2, (uint16_t)respLen);
  }

  return (uint16_t)respLen;
}


Status_t Lebin_FillSeries(uint8_t *buffer, uint32_t id, uint32_t timestamp, uint32_t delta,
                          uint32_t count)
{
  uint32_t length = LEBIN_HEADER_LEN + 16 + count * CONF_BYTE_LEN_ID(id);

  buffer[0] = LEBIN_START_BYTE;
  buffer[1] = LEBIN_TIME_SER;
  Bytes_PutU16Le(buffer + 2, (uint16_t)length);
  Bytes_PutU32Le(buffer + 4, id);
  Bytes_PutU32Le(buffer + 8, timestamp);
  Bytes_PutU32Le(buffer + 12, delta);
  Bytes_PutU32Le(buffer + 16, count);

  return (length > 0xFFFFu) ? STATUS_ERROR : STATUS_OK;
}


uint32_t Lebin_GetErrorCount(void)
{
  return lb.errors;
}

/* Private Functions ---------------------------------------------------------*/

static void Lebin_Drop(uint32_t count)
{
  memmove(lb.packet, lb.packet + count, lb.packetLen - count);
  lb.packetLen -= count;
}


/** Response: ID + value for every known ID; unknown IDs are left out */
static uint32_t Lebin_ReadReg(const uint8_t *req, uint32_t length, uint8_t *resp)
{
  uint32_t reqIdx = LEBIN_HEADER_LEN;
  uint32_t respIdx = LEBIN_HEADER_LEN;
  uint32_t id;
  uint32_t size;

  resp[1] = LEBIN_READ_REG_RESP;

  while (reqIdx + 4 <= length)
  {
    id = Bytes_GetU32Le(req + reqIdx);
    reqIdx += 4;
    if (Config_CheckLimits(id) != STATUS_OK)
    {
      continue;
    }
    size = CONF_BYTE_LEN_ID(id);
    if (respIdx + 4 + size > LEBIN_PACKET_MAX)
    {
      break;
    }
    Bytes_PutU32Le(resp + respIdx, id);
    memcpy(resp + respIdx + 4, CONF_PTR(id), size);
    respIdx += 4 + size;
  }

  return respIdx;
}


/** Response: ID + stored value for every pair; 0xF0 bytes for unknown IDs */
static uint32_t Lebin_WriteReg(const uint8_t *req, uint32_t length, uint8_t *resp)
{
  uint32_t idx = LEBIN_HEADER_LEN;
  uint32_t id;
  uint32_t size;

  resp[1] = LEBIN_READ_REG_RESP;

  while (idx + 4 <= length)
  {
    id = Bytes_GetU32Le(req + idx);
    size = CONF_BYTE_LEN_ID(id);
    if (idx + 4 + size > length)
    {
      break;
    }
    Bytes_PutU32Le(resp + idx, id);
    if (Config_CheckLimits(id) == STATUS_OK)
    {
      memcpy(CONF_PTR(id), req + idx + 4, size);
      (void)Config_ApplyConfig(id);
      memcpy(resp + idx + 4, CONF_PTR(id), size);
    }
    else
    {
      memset(resp + idx + 4, 0xF0, size);
    }
    idx += 4 + size;
  }

  return idx;
}

/* [upgrade function] */

#endif /* REGMAP_LIB_LEBIN */

/** @} */
```

- [ ] **Step 7: Run tests to verify they pass**

Run: `pytest tests/test_firmware_core.py -q`
Expected: PASS

- [ ] **Step 8: Commit**

```bash
git add src/regmap/firmware_lib tests/c
git commit -m "feat: LeBin protocol core of the firmware library"
```

---

### Task 5: Firmware upgrade (shared backend, LeBin and Modbus paths)

**Files:**
- Create: `src/regmap/firmware_lib/core/fw_upgrade.h`, `fw_upgrade.c`
- Create: `src/regmap/firmware_lib/core/mb_upgrade.h`, `mb_upgrade.c`
- Modify: `src/regmap/firmware_lib/core/port.h` (upgrade section)
- Modify: `src/regmap/firmware_lib/core/lebin.c` (replace `/* [upgrade ...] */` anchors)
- Modify: `src/regmap/firmware_lib/core/modbus_slave.c` (replace `/* [upgrade ...] */` anchors)
- Modify: `tests/c/fake_port.h`, `tests/c/fake_port.c`, `tests/c/test_main.c`
- Create: `tests/c/test_upgrade.c`

**Interfaces:**
- Consumes: Tasks 3 and 4.
- Produces: `Status_t Upgrade_Begin(uint32_t size)`, `Status_t Upgrade_Write(uint32_t offset, const uint8_t *data, uint32_t len)`, `Status_t Upgrade_Finish(void)`, `void Upgrade_Apply(void)`, `bool Upgrade_IsActive(void)`; `MB_UPGR_BASE_ADDRESS` (1000), `MB_UPGR_PAGE_SIZE` (64), `MB_UPGR_END_ADDRESS` (1041), `MbUpgr_Mode_t`, `MbUpgr_Status_t`, `Status_t MbUpgr_Init(void)`, `Status_t MbUpgr_Handle(void)`, `Status_t MbUpgr_WriteRegisters(uint16_t address, uint16_t count, const uint8_t *data)`, `Status_t MbUpgr_ReadRegisters(uint16_t address, uint16_t count, uint8_t *data)`, `uint16_t MbUpgr_GetType(void)`, `uint16_t MbUpgr_GetMode(void)`; port: `Status_t UpgradePort_Begin(uint32_t size)`, `Status_t UpgradePort_Write(uint32_t offset, const uint8_t *data, uint32_t len)`, `Status_t UpgradePort_Verify(uint32_t size)`, `void UpgradePort_Apply(void)`.

Modbus upgrade register map (wire compatible with the old `mb_upgrade.c`; register `n` = storage bytes `2n..2n+1`, little-endian words, big-endian on the wire):

| register | content |
|---|---|
| 1000 | type |
| 1001 | mode (0 erase on the fly, 1 erase at start, 2 apply) |
| 1002–1003 | size (low word first) |
| 1004 | page size in bytes (≤ 64) |
| 1005–1006 | page offset (low word first) |
| 1007–1038 | page data (64 bytes) |
| 1039 | status (0 busy, 1 ready, 2 done OK, 3 done error) |
| 1040 | write done (master writes 1 after a page) |
| 1041 | reserved |

- [ ] **Step 1: Extend the fake port**

Append to `tests/c/fake_port.h` before `#endif`:

```c
/* Upgrade */
#define FAKE_FLASH_SIZE 4096u

extern uint8_t fake_flash[FAKE_FLASH_SIZE];
extern int fake_upgrade_begin_count;
extern uint32_t fake_upgrade_begin_size;
extern int fake_verify_count;
extern uint32_t fake_verify_size;
extern Status_t fake_verify_result;
extern int fake_apply_count;

void fake_upgrade_reset(void);
```

Append to `tests/c/fake_port.c`:

```c
/* Upgrade -------------------------------------------------------------------*/

uint8_t fake_flash[FAKE_FLASH_SIZE];
int fake_upgrade_begin_count;
uint32_t fake_upgrade_begin_size;
int fake_verify_count;
uint32_t fake_verify_size;
Status_t fake_verify_result;
int fake_apply_count;

Status_t UpgradePort_Begin(uint32_t size)
{
  fake_upgrade_begin_count++;
  fake_upgrade_begin_size = size;
  memset(fake_flash, 0xFF, sizeof(fake_flash));
  return (size <= FAKE_FLASH_SIZE) ? STATUS_OK : STATUS_ERROR;
}

Status_t UpgradePort_Write(uint32_t offset, const uint8_t *data, uint32_t len)
{
  if (offset + len > FAKE_FLASH_SIZE)
  {
    return STATUS_ERROR;
  }
  memcpy(fake_flash + offset, data, len);
  return STATUS_OK;
}

Status_t UpgradePort_Verify(uint32_t size)
{
  fake_verify_count++;
  fake_verify_size = size;
  return fake_verify_result;
}

void UpgradePort_Apply(void)
{
  fake_apply_count++;
}

void fake_upgrade_reset(void)
{
  fake_upgrade_begin_count = 0;
  fake_upgrade_begin_size = 0;
  fake_verify_count = 0;
  fake_verify_size = 0;
  fake_verify_result = STATUS_OK;
  fake_apply_count = 0;
}
```

In `tests/c/test_main.c` declare `void test_upgrade(void);` and call it after `test_lebin();`.

- [ ] **Step 2: Write the failing tests `tests/c/test_upgrade.c`**

```c
#include "check.h"
#include "config_app.h"
#include "configuration.h"
#include "fake_port.h"
#include "fw_upgrade.h"
#include "lebin.h"
#include "lib_bytes.h"
#include "mb_upgrade.h"
#include "modbus_slave.h"

static void test_session_rules(void)
{
  uint8_t data[32] = {0};

  fake_upgrade_reset();
  (void)Upgrade_Finish(); /* end a session left by earlier tests */
  CHECK(Upgrade_Write(0, data, sizeof(data)) == STATUS_ERROR);
  CHECK(Upgrade_Finish() == STATUS_ERROR);
  CHECK(Upgrade_Begin(0) == STATUS_OK);
  CHECK(Upgrade_IsActive());
  CHECK(Upgrade_Write(0, data, 32) == STATUS_OK);
  CHECK(Upgrade_Write(32, data, 32) == STATUS_OK);
  CHECK(Upgrade_Finish() == STATUS_OK);
  CHECK(fake_verify_size == 64);
  CHECK(!Upgrade_IsActive());
  CHECK(Upgrade_Write(64, data, 32) == STATUS_ERROR);
}

/* LeBin FW upgrade packet with `len` data bytes at `offset`; runs Lebin_Handle once. */
static void lb_upgrade(uint32_t offset, const uint8_t *data, uint16_t len)
{
  uint8_t pkt[64];

  pkt[0] = LEBIN_START_BYTE;
  pkt[1] = LEBIN_FW_UPGRADE;
  Bytes_PutU16Le(pkt + 2, (uint16_t)(8 + len));
  Bytes_PutU32Le(pkt + 4, offset);
  memcpy(pkt + 8, data, len);
  Lebin_RxBytes(pkt, (uint32_t)(8 + len));
  Lebin_Handle();
}

static void test_lebin_upgrade(void)
{
  uint8_t data[32];
  uint8_t i;

  for (i = 0; i < sizeof(data); i++)
  {
    data[i] = i;
  }
  fake_tick = 1000;
  fake_lebin_reset();
  fake_upgrade_reset();
  Config_Init();
  Lebin_Init();

  lb_upgrade(0, data, 32);
  CHECK(fake_upgrade_begin_count == 1);
  CHECK(memcmp(fake_flash, data, 32) == 0);
  CHECK_BYTES(fake_lebin_tx, fake_lebin_tx_len,
              0x90, 0x7F, 0x0C, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

  lb_upgrade(32, data, 32);
  CHECK_BYTES(fake_lebin_tx, fake_lebin_tx_len,
              0x90, 0x7F, 0x0C, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

  lb_upgrade(64, data, 10); /* not a multiple of 32 */
  CHECK(fake_lebin_tx[8] == 2);

  lb_upgrade(64, data, 0); /* last packet */
  CHECK(fake_verify_count == 1);
  CHECK(fake_verify_size == 64);
  CHECK_BYTES(fake_lebin_tx, fake_lebin_tx_len,
              0x90, 0x7F, 0x0C, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

  fake_verify_result = STATUS_ERROR;
  lb_upgrade(0, data, 32);
  lb_upgrade(32, data, 0);
  CHECK(fake_lebin_tx[8] == 1);
}

/* Modbus helpers (slave 1, no timeout) */

static void mb_setup(void)
{
  MbSlave_Config_t cfg;

  fake_tick = 1000;
  fake_mb_reset();
  fake_upgrade_reset();
  Config_Init();
  conf.com.mb_address = 1;
  conf.com.mb_timeout = 0;
  ConfigApp_ModbusConfig(&cfg);
  MbSlave_Init(&cfg);
}

/* Send a request, then finish the response so that MbUpgr_Handle runs. */
static void mb_transaction(const uint8_t *pdu, uint16_t len)
{
  uint8_t frame[MBSLAVE_FRAME_MAX];

  memcpy(frame, pdu, len);
  Bytes_PutU16Le(frame + len, MbSlave_Crc16(frame, len));
  MbSlave_RxFrame(frame, (uint16_t)(len + 2));
  MbSlave_Handle();
  MbSlave_TxDone();
  MbSlave_Handle();
}

static uint16_t mb_read_status(void)
{
  const uint8_t req[] = {0x01, 0x03, 0x04, 0x0F, 0x00, 0x01}; /* register 1039 */

  mb_transaction(req, sizeof(req));
  return Bytes_GetU16Be(fake_mb_tx + 3);
}

static void test_modbus_upgrade(void)
{
  /* Header at 1000: type 1, mode 0 (erase on the fly), size 64 */
  const uint8_t header[] = {0x01, 0x10, 0x03, 0xE8, 0x00, 0x04, 0x08,
                            0x00, 0x01, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00};
  const uint8_t writeDone[] = {0x01, 0x06, 0x04, 0x10, 0x00, 0x01}; /* register 1040 */
  uint8_t page[7 + 70];
  uint8_t expected[64];
  uint8_t i;

  mb_setup();
  mb_transaction(header, sizeof(header));
  CHECK(fake_upgrade_begin_count == 1);
  CHECK(fake_upgrade_begin_size == 0);
  CHECK(MbUpgr_GetType() == 1);
  CHECK(mb_read_status() == MB_UPGR_STATUS_READY);

  /* Page at 1004: page size 64, offset 0, data bytes 0..63 (word bytes swapped on the wire) */
  page[0] = 0x01;
  page[1] = 0x10;
  Bytes_PutU16Be(page + 2, 1004);
  Bytes_PutU16Be(page + 4, 35);
  page[6] = 70;
  Bytes_PutU16Be(page + 7, 64);
  Bytes_PutU16Be(page + 9, 0);
  Bytes_PutU16Be(page + 11, 0);
  for (i = 0; i < 64; i++)
  {
    expected[i] = i;
    page[13 + (i ^ 1u)] = i;
  }
  mb_transaction(page, sizeof(page));
  mb_transaction(writeDone, sizeof(writeDone));
  CHECK(memcmp(fake_flash, expected, sizeof(expected)) == 0);
  CHECK(fake_verify_count == 1);
  CHECK(fake_verify_size == 64);
  CHECK(mb_read_status() == MB_UPGR_STATUS_DONE_OK);
}

static void test_modbus_upgrade_apply_and_range(void)
{
  const uint8_t apply[] = {0x01, 0x10, 0x03, 0xE8, 0x00, 0x02, 0x04, 0x00, 0x01, 0x00, 0x02};
  const uint8_t beyond[] = {0x01, 0x03, 0x04, 0x12, 0x00, 0x01};  /* 1042 */
  const uint8_t overlap[] = {0x01, 0x03, 0x04, 0x10, 0x00, 0x03}; /* 1040..1042 */

  mb_setup();
  mb_transaction(apply, sizeof(apply));
  CHECK(fake_apply_count == 1);
  mb_transaction(beyond, sizeof(beyond));
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x83, 0x02);
  mb_transaction(overlap, sizeof(overlap));
  CHECK_BYTES(fake_mb_tx, fake_mb_tx_len - 2, 0x01, 0x83, 0x02);
}

void test_upgrade(void)
{
  test_session_rules();
  test_lebin_upgrade();
  test_modbus_upgrade();
  test_modbus_upgrade_apply_and_range();
}
```

- [ ] **Step 3: Run to verify it fails**

Run: `pytest tests/test_firmware_core.py -q`
Expected: FAIL — `fw_upgrade.h: No such file or directory`

- [ ] **Step 4: Add the upgrade section to `core/port.h`** (before `#endif /* PORT_H_ */`)

```c
/* Firmware upgrade ----------------------------------------------------------*/

#if REGMAP_LIB_UPGRADE
/** Start a new image; size 0 = unknown (erase on the fly), otherwise erase the whole size */
Status_t UpgradePort_Begin(uint32_t size);

/** Program len bytes at offset of the upgrade area; erases units the write enters. data may
 * be unaligned. */
Status_t UpgradePort_Write(uint32_t offset, const uint8_t *data, uint32_t len);

/** Check the received image of the given size */
Status_t UpgradePort_Verify(uint32_t size);

/** Apply the new image (e.g. request a restart into the bootloader) */
void UpgradePort_Apply(void);
#endif
```

- [ ] **Step 5: Create `core/fw_upgrade.h` and `core/fw_upgrade.c`**

`fw_upgrade.h`:

```c
/**
 * @file       fw_upgrade.h
 * @brief      Firmware upgrade session shared by LeBin and Modbus
 * @regmap-lib
 */
#ifndef FW_UPGRADE_H_
#define FW_UPGRADE_H_

#include "common.h"
#include "regmap_lib_conf.h"

/** Start a session; size 0 = unknown */
Status_t Upgrade_Begin(uint32_t size);

/** Write image data; STATUS_ERROR outside a session (a failed write ends the session) */
Status_t Upgrade_Write(uint32_t offset, const uint8_t *data, uint32_t len);

/** End the session and verify the received image */
Status_t Upgrade_Finish(void);

/** Apply the new image (port decides how) */
void Upgrade_Apply(void);

/** True between Upgrade_Begin and Upgrade_Finish */
bool Upgrade_IsActive(void);

#endif /* FW_UPGRADE_H_ */
```

`fw_upgrade.c`:

```c
/**
 * @file       fw_upgrade.c
 * @brief      Firmware upgrade session shared by LeBin and Modbus
 * @regmap-lib
 */

#include "fw_upgrade.h"

#if REGMAP_LIB_UPGRADE

#include "port.h"

typedef struct
{
  bool active;        ///< Session running
  uint32_t received;  ///< End of the highest written data
} Upgrade_Private_t;

static Upgrade_Private_t upg;

Status_t Upgrade_Begin(uint32_t size)
{
  Status_t ret = UpgradePort_Begin(size);

  upg.active = (ret == STATUS_OK);
  upg.received = 0;

  return ret;
}


Status_t Upgrade_Write(uint32_t offset, const uint8_t *data, uint32_t len)
{
  if (!upg.active)
  {
    return STATUS_ERROR;
  }
  if (UpgradePort_Write(offset, data, len) != STATUS_OK)
  {
    upg.active = false;
    return STATUS_ERROR;
  }
  if (offset + len > upg.received)
  {
    upg.received = offset + len;
  }

  return STATUS_OK;
}


Status_t Upgrade_Finish(void)
{
  if (!upg.active)
  {
    return STATUS_ERROR;
  }
  upg.active = false;

  return UpgradePort_Verify(upg.received);
}


void Upgrade_Apply(void)
{
  UpgradePort_Apply();
}


bool Upgrade_IsActive(void)
{
  return upg.active;
}

#endif /* REGMAP_LIB_UPGRADE */
```

- [ ] **Step 6: Create `core/mb_upgrade.h` and `core/mb_upgrade.c`**

`mb_upgrade.h`:

```c
/**
 * @file       mb_upgrade.h
 * @brief      Firmware upgrade over Modbus RTU holding registers
 * @regmap-lib
 *
 * @defgroup grMbUpgrade Modbus upgrade
 * @{
 * @brief Firmware received in pages through holding registers from MB_UPGR_BASE_ADDRESS
 *
 * The master writes the header (type, mode, size), then for every page the page size, offset
 * and data followed by write-done = 1. The slave programs the page in MbUpgr_Handle and sets
 * the status to ready (or done after the last page). Called from modbus_slave.c.
 */
#ifndef MB_UPGRADE_H_
#define MB_UPGRADE_H_

#include "common.h"
#include "regmap_lib_conf.h"

/** Base address of upgrade registers */
#define MB_UPGR_BASE_ADDRESS    1000

/** Size of page in bytes */
#define MB_UPGR_PAGE_SIZE       64

/** Last address of upgrade registers */
#define MB_UPGR_END_ADDRESS     (MB_UPGR_BASE_ADDRESS + MB_UPGR_PAGE_SIZE / 2 + 9)

typedef enum
{
  MB_UPGR_MODE_ERASE_ON_FLY = 0,
  MB_UPGR_MODE_ERASE_AT_START = 1,
  MB_UPGR_MODE_APPLY = 2,
} MbUpgr_Mode_t;

typedef enum
{
  MB_UPGR_STATUS_BUSY = 0,
  MB_UPGR_STATUS_READY = 1,
  MB_UPGR_STATUS_DONE_OK = 2,
  MB_UPGR_STATUS_DONE_ERROR = 3,
} MbUpgr_Status_t;

/** Reset registers, status busy */
Status_t MbUpgr_Init(void);

/** Start the session after a header write, program a page after write-done; call when idle */
Status_t MbUpgr_Handle(void);

/** Write `count` registers from `address` (wire data, big-endian words) */
Status_t MbUpgr_WriteRegisters(uint16_t address, uint16_t count, const uint8_t *data);

/** Read `count` registers from `address` into wire data */
Status_t MbUpgr_ReadRegisters(uint16_t address, uint16_t count, uint8_t *data);

/** Value of the type register */
uint16_t MbUpgr_GetType(void);

/** Value of the mode register */
uint16_t MbUpgr_GetMode(void);

#endif /* MB_UPGRADE_H_ */
/** @} */
```

`mb_upgrade.c`:

```c
/**
 * @file       mb_upgrade.c
 * @brief      Firmware upgrade over Modbus RTU holding registers
 * @regmap-lib
 * @addtogroup grMbUpgrade
 * @{
 */

#include "mb_upgrade.h"

#if REGMAP_LIB_UPGRADE

#include "fw_upgrade.h"

/** Register storage: register n = bytes 2n..2n+1 (little-endian words) */
typedef struct __packed
{
  uint16_t type;                     ///< Type of binary (application specific)
  uint16_t mode;                     ///< MbUpgr_Mode_t
  uint32_t size;                     ///< Size of binary in bytes
  uint16_t page_size;                ///< Size of current page in bytes
  uint32_t offset;                   ///< Offset of current page
  uint8_t data[MB_UPGR_PAGE_SIZE];   ///< Current page data
  uint16_t status;                   ///< MbUpgr_Status_t
  uint16_t writeDone;                ///< 1 = page complete, program it
  uint16_t reserved;                 ///< Last register of the range
} MbUpgr_Registers_t;

/* The storage must cover the whole register range */
typedef char MbUpgr_SizeCheck_t[(sizeof(MbUpgr_Registers_t)
                                 == (MB_UPGR_END_ADDRESS - MB_UPGR_BASE_ADDRESS + 1) * 2) ? 1 : -1];

typedef struct
{
  MbUpgr_Registers_t regs;
  bool headerWritten;  ///< One of the first 4 registers written
} MbUpgr_Private_t;

static MbUpgr_Private_t mbu;

static bool MbUpgr_InRange(uint16_t address, uint16_t count)
{
  return address >= MB_UPGR_BASE_ADDRESS && count > 0
         && (uint32_t)address + count - 1u <= MB_UPGR_END_ADDRESS;
}


Status_t MbUpgr_Init(void)
{
  memset(&mbu, 0, sizeof(mbu));
  mbu.regs.status = MB_UPGR_STATUS_BUSY;

  return STATUS_OK;
}


Status_t MbUpgr_Handle(void)
{
  Status_t ret = STATUS_OK;

  if (mbu.headerWritten)
  {
    mbu.headerWritten = false;
    switch (mbu.regs.mode)
    {
      case MB_UPGR_MODE_ERASE_AT_START:
        ret = Upgrade_Begin(mbu.regs.size);
        break;
      case MB_UPGR_MODE_APPLY:
        Upgrade_Apply();
        break;
      default:
        ret = Upgrade_Begin(0);
        break;
    }
    if (mbu.regs.writeDone == 0)
    {
      mbu.regs.status = (ret == STATUS_OK) ? MB_UPGR_STATUS_READY : MB_UPGR_STATUS_DONE_ERROR;
    }
  }

  if (mbu.regs.writeDone != 0)
  {
    if (mbu.regs.page_size > MB_UPGR_PAGE_SIZE)
    {
      ret = STATUS_ERROR;
    }
    else
    {
      ret = Upgrade_Write(mbu.regs.offset, mbu.regs.data, mbu.regs.page_size);
    }
    mbu.regs.status = (ret == STATUS_OK) ? MB_UPGR_STATUS_READY : MB_UPGR_STATUS_DONE_ERROR;

    /* Last page received? */
    if (ret == STATUS_OK && mbu.regs.size - mbu.regs.offset <= mbu.regs.page_size)
    {
      ret = Upgrade_Finish();
      mbu.regs.status = (ret == STATUS_OK) ? MB_UPGR_STATUS_DONE_OK : MB_UPGR_STATUS_DONE_ERROR;
    }
    mbu.regs.writeDone = 0;
  }

  return ret;
}


Status_t MbUpgr_WriteRegisters(uint16_t address, uint16_t count, const uint8_t *data)
{
  uint8_t *storage;
  uint16_t i;

  if (!MbUpgr_InRange(address, count))
  {
    return STATUS_ERROR;
  }
  storage = (uint8_t *)&mbu.regs + (address - MB_UPGR_BASE_ADDRESS) * 2;

  for (i = 0; i < count; i++)
  {
    storage[i * 2 + 1] = data[i * 2 + 0];
    storage[i * 2 + 0] = data[i * 2 + 1];
  }

  if (address - MB_UPGR_BASE_ADDRESS < 4)
  {
    mbu.headerWritten = true;
  }

  return STATUS_OK;
}


Status_t MbUpgr_ReadRegisters(uint16_t address, uint16_t count, uint8_t *data)
{
  const uint8_t *storage;
  uint16_t i;

  if (!MbUpgr_InRange(address, count))
  {
    return STATUS_ERROR;
  }
  storage = (const uint8_t *)&mbu.regs + (address - MB_UPGR_BASE_ADDRESS) * 2;

  for (i = 0; i < count; i++)
  {
    data[i * 2 + 1] = storage[i * 2 + 0];
    data[i * 2 + 0] = storage[i * 2 + 1];
  }

  return STATUS_OK;
}


uint16_t MbUpgr_GetType(void)
{
  return mbu.regs.type;
}


uint16_t MbUpgr_GetMode(void)
{
  return mbu.regs.mode;
}

#endif /* REGMAP_LIB_UPGRADE */

/** @} */
```

- [ ] **Step 7: Wire the upgrade into `core/lebin.c`** (replace each anchor comment exactly)

`/* [upgrade include] */` →

```c
#if REGMAP_LIB_UPGRADE
#include "fw_upgrade.h"
#endif

/* FW upgrade return codes in the acknowledge packet */
#define LEBIN_UPG_OK            0u
#define LEBIN_UPG_ERROR         1u
#define LEBIN_UPG_WRONG_LENGTH  2u
#define LEBIN_UPG_ALIGN         32u   ///< Data length must be a multiple of this
```

`/* [upgrade prototype] */` →

```c
#if REGMAP_LIB_UPGRADE
static uint32_t Lebin_FwUpgrade(const uint8_t *req, uint32_t length, uint8_t *resp);
#endif
```

`    /* [upgrade case] */` →

```c
#if REGMAP_LIB_UPGRADE
    case LEBIN_FW_UPGRADE:
      respLen = Lebin_FwUpgrade(req, length, resp);
      break;
#endif
```

`/* [upgrade function] */` →

```c
#if REGMAP_LIB_UPGRADE
/**
 * Request: offset (4 bytes) + data; data length 0 ends the image.
 * Acknowledge: offset + data length, return code.
 */
static uint32_t Lebin_FwUpgrade(const uint8_t *req, uint32_t length, uint8_t *resp)
{
  uint32_t offset = Bytes_GetU32Le(req + 4);
  uint32_t dataLength = length - LEBIN_MIN_LENGTH;
  uint32_t retCode;

  if (dataLength == 0)
  {
    retCode = (Upgrade_Finish() == STATUS_OK) ? LEBIN_UPG_OK : LEBIN_UPG_ERROR;
  }
  else if (dataLength % LEBIN_UPG_ALIGN != 0)
  {
    retCode = LEBIN_UPG_WRONG_LENGTH;
  }
  else
  {
    if (offset == 0)
    {
      (void)Upgrade_Begin(0);
    }
    retCode = (Upgrade_Write(offset, req + LEBIN_MIN_LENGTH, dataLength) == STATUS_OK)
              ? LEBIN_UPG_OK : LEBIN_UPG_ERROR;
  }

  resp[1] = LEBIN_FW_UPG_ACK;
  Bytes_PutU32Le(resp + 4, offset + dataLength);
  Bytes_PutU32Le(resp + 8, retCode);

  return 12;
}
#endif
```

- [ ] **Step 8: Wire the upgrade into `core/modbus_slave.c`** (replace each anchor comment exactly)

`/* [upgrade include] */` →

```c
#if REGMAP_LIB_UPGRADE
#include "mb_upgrade.h"
#endif
```

`  /* [upgrade init] */` →

```c
#if REGMAP_LIB_UPGRADE
  MbUpgr_Init();
#endif
```

`  /* [upgrade handle] */` →

```c
#if REGMAP_LIB_UPGRADE
  if (!mb.txBusy)
  {
    (void)MbUpgr_Handle();
  }
#endif
```

`/* [upgrade range] */` →

```c
#if REGMAP_LIB_UPGRADE
static bool MbSlave_InUpgrade(uint16_t addr, uint16_t count)
{
  return addr >= MB_UPGR_BASE_ADDRESS && (uint32_t)addr + count - 1u <= MB_UPGR_END_ADDRESS;
}
#endif
```

`    /* [upgrade read] */` →

```c
#if REGMAP_LIB_UPGRADE
    if (fc == MB_FC_READ_HOLDING && MbSlave_InUpgrade(addr, count))
    {
      (void)MbUpgr_ReadRegisters(addr, count, resp + 3);
    }
    else
#endif
```

`    /* [upgrade write single] */` →

```c
#if REGMAP_LIB_UPGRADE
    if (MbSlave_InUpgrade(addr, 1))
    {
      (void)MbUpgr_WriteRegisters(addr, 1, req + 4);
    }
    else
#endif
```

`  /* [upgrade write multiple] */` →

```c
#if REGMAP_LIB_UPGRADE
  if (MbSlave_InUpgrade(addr, count))
  {
    (void)MbUpgr_WriteRegisters(addr, count, req + 7);
  }
  else
#endif
```

- [ ] **Step 9: Run tests to verify they pass**

Run: `pytest tests/test_firmware_core.py -q`
Expected: PASS. Then check no anchor is left: `grep -rn "\[upgrade" src/regmap/firmware_lib` prints nothing.

- [ ] **Step 10: Commit**

```bash
git add src/regmap/firmware_lib tests/c
git commit -m "feat: firmware upgrade over LeBin and Modbus in the firmware library"
```

---

### Task 6: STM32 HAL ports

**Files:**
- Create: `src/regmap/firmware_lib/port/port_stm32.h`, `port_stm32.c`
- Create: `src/regmap/firmware_lib/port/modbus_port_stm32.c`
- Create: `src/regmap/firmware_lib/port/lebin_port_usb_cdc.c`
- Create: `src/regmap/firmware_lib/port/lebin_port_uart.c`
- Create: `src/regmap/firmware_lib/port/upgrade_port_stm32.c`
- Create: `tests/c/hal_stub/main.h`, `tests/c/hal_stub/usbd_cdc.h`
- Test: `tests/test_firmware_ports.py`

**Interfaces:**
- Consumes: `port.h` (all sections), `Lebin_RxBytes`, `MbSlave_RxFrame`, `MbSlave_TxDone`, settings from `regmap_lib_conf.h`.
- Produces: implementations of every `port.h` function; `void PortStm32_UartRxEvent(UART_HandleTypeDef *huart, uint16_t size)`, `void PortStm32_UartTxCplt(UART_HandleTypeDef *huart)`, `void PortStm32_UartError(UART_HandleTypeDef *huart)`, `void LebinPortUsb_Receive(uint8_t *buf, uint32_t len)` (called from CubeMX `CDC_Receive_xS`), internal `MbPortStm32_*` / `LebinPortUart_*` handlers.

The HAL stub only mirrors the HAL API names and signatures used here so that `gcc -fsyntax-only -Werror` catches typos and type errors; the real check is the build on a target (Task 8).

- [ ] **Step 1: Write the failing test `tests/test_firmware_ports.py`**

```python
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
    "modbus_port_stm32.c": [[], ["-DMB_PORT_USE_DMA=1"], ["-DMB_PORT_HW_DE=1"]],
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
```

`MB_PORT_HW_DE` needs the DE GPIO lines gone; the variant passes `-DMB_PORT_HW_DE=1` and the port code ignores `MB_PORT_DE_GPIO_*` when `MB_PORT_HW_DE` is defined.

- [ ] **Step 2: Create the HAL stub**

`tests/c/hal_stub/main.h`:

```c
/* Minimal STM32 HAL/CMSIS stand-in for syntax checks of the ports. Signatures follow the
 * STM32Cube HAL; only what the ports use is declared. */
#ifndef MAIN_H_
#define MAIN_H_

#include <stdint.h>

#define __packed            __attribute__((packed))
#define __aligned(x)        __attribute__((aligned(x)))
#define __IO                volatile

typedef enum { HAL_OK = 0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;

uint32_t HAL_GetTick(void);
uint32_t __get_PRIMASK(void);
void __set_PRIMASK(uint32_t primask);
void __disable_irq(void);
uint32_t __REV16(uint32_t value);

/* GPIO */
typedef struct { uint32_t ODR; } GPIO_TypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;
#define GPIOA       ((GPIO_TypeDef *)0x48000000u)
#define GPIO_PIN_1  ((uint16_t)0x0002)
void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState);

/* DMA */
typedef struct { uint32_t Instance; } DMA_HandleTypeDef;
#define DMA_IT_HT   0x00000008u
#define __HAL_DMA_DISABLE_IT(handle, it)  ((void)(handle), (void)(it))

/* UART */
typedef struct
{
  uint32_t BaudRate;
  uint32_t WordLength;
  uint32_t StopBits;
  uint32_t Parity;
} UART_InitTypeDef;

typedef uint32_t HAL_UART_StateTypeDef;
#define HAL_UART_STATE_READY  0x20u

typedef struct
{
  UART_InitTypeDef Init;
  __IO HAL_UART_StateTypeDef gState;
  DMA_HandleTypeDef *hdmarx;
} UART_HandleTypeDef;

#define UART_WORDLENGTH_8B     0x00000000u
#define UART_WORDLENGTH_9B     0x00001000u
#define UART_STOPBITS_1        0x00000000u
#define UART_STOPBITS_2        0x00002000u
#define UART_PARITY_NONE       0x00000000u
#define UART_PARITY_EVEN       0x00000400u
#define UART_PARITY_ODD        0x00000600u
#define UART_DE_POLARITY_HIGH  0x00000000u

HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *huart);
HAL_StatusTypeDef HAL_RS485Ex_Init(UART_HandleTypeDef *huart, uint32_t Polarity,
                                   uint32_t AssertionTime, uint32_t DeassertionTime);
HAL_StatusTypeDef HAL_UART_Abort(UART_HandleTypeDef *huart);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *huart);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *huart, const uint8_t *pData,
                                       uint16_t Size);
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *huart, const uint8_t *pData,
                                        uint16_t Size);
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_IT(UART_HandleTypeDef *huart, uint8_t *pData,
                                              uint16_t Size);
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *huart, uint8_t *pData,
                                               uint16_t Size);
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart);

/* FLASH */
#define FLASH_BASE  0x08000000u

typedef struct
{
  uint32_t TypeErase;
  uint32_t Banks;
#ifdef STUB_FAMILY_PAGE
  uint32_t Page;
  uint32_t NbPages;
#else
  uint32_t Sector;
  uint32_t NbSectors;
  uint32_t VoltageRange;
#endif
} FLASH_EraseInitTypeDef;

#ifdef STUB_FAMILY_PAGE
/* Page-based family (G4-like, dual bank) */
#define FLASH_TYPEERASE_PAGES        0x00u
#define FLASH_TYPEPROGRAM_DOUBLEWORD 0x00u
#define FLASH_PAGE_SIZE              0x800u
#define FLASH_BANK_SIZE              0x40000u
#define FLASH_BANK_1                 0x01u
#define FLASH_BANK_2                 0x02u
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t TypeProgram, uint32_t Address, uint64_t Data);
#else
/* STM32H7 (dual bank) */
#define DUAL_BANK
#define FLASH_TYPEERASE_SECTORS          0x00u
#define FLASH_TYPEPROGRAM_FLASHWORD      0x01u
#define FLASH_NB_32BITWORD_IN_FLASHWORD  8u
#define FLASH_SECTOR_SIZE                0x00020000u
#define FLASH_BANK1_BASE                 0x08000000u
#define FLASH_BANK2_BASE                 0x08100000u
#define FLASH_BANK_1                     0x01u
#define FLASH_BANK_2                     0x02u
#define FLASH_VOLTAGE_RANGE_3            0x20u
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t TypeProgram, uint32_t FlashAddress,
                                    uint32_t DataAddress);
#endif

HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *pEraseInit, uint32_t *SectorError);

#endif /* MAIN_H_ */
```

`tests/c/hal_stub/usbd_cdc.h`:

```c
/* Minimal ST USB Device Library CDC stand-in for syntax checks of the ports. */
#ifndef USBD_CDC_H_
#define USBD_CDC_H_

#include <stdint.h>

#define USBD_OK 0u

typedef struct
{
  void *pClassData;
} USBD_HandleTypeDef;

typedef struct
{
  volatile uint32_t TxState;
} USBD_CDC_HandleTypeDef;

uint8_t USBD_CDC_SetTxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff, uint32_t length);
uint8_t USBD_CDC_SetRxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff);
uint8_t USBD_CDC_TransmitPacket(USBD_HandleTypeDef *pdev);
uint8_t USBD_CDC_ReceivePacket(USBD_HandleTypeDef *pdev);

#endif /* USBD_CDC_H_ */
```

- [ ] **Step 3: Run to verify it fails**

Run: `pytest tests/test_firmware_ports.py -q`
Expected: FAIL — `test_every_stm32_port_file_is_checked` (files missing) and `KeyError`/file-not-found in the parametrised tests.

- [ ] **Step 4: Create `port/port_stm32.h`**

```c
/**
 * @file       port_stm32.h
 * @brief      STM32 HAL port of the regmap library: internal interface
 *
 * Port file: written once by `regmap export-lib`, then owned by the project.
 */
#ifndef PORT_STM32_H_
#define PORT_STM32_H_

#include "common.h"
#include "regmap_lib_conf.h"

/* Dispatch of HAL UART callbacks to the library ports. Call them from your own HAL callbacks
 * when REGMAP_LIB_HAL_UART_CALLBACKS is 0. */
void PortStm32_UartRxEvent(UART_HandleTypeDef *huart, uint16_t size);
void PortStm32_UartTxCplt(UART_HandleTypeDef *huart);
void PortStm32_UartError(UART_HandleTypeDef *huart);

/* Modbus port (modbus_port_stm32.c) */
void MbPortStm32_RxEvent(UART_HandleTypeDef *huart, uint16_t size);
void MbPortStm32_TxCplt(UART_HandleTypeDef *huart);
void MbPortStm32_Error(UART_HandleTypeDef *huart);

/* LeBin UART port (lebin_port_uart.c) */
void LebinPortUart_RxEvent(UART_HandleTypeDef *huart, uint16_t size);
void LebinPortUart_Error(UART_HandleTypeDef *huart);

/* LeBin USB CDC port (lebin_port_usb_cdc.c): call from CDC_Receive_FS/HS in usbd_cdc_if.c */
void LebinPortUsb_Receive(uint8_t *buf, uint32_t len);

#endif /* PORT_STM32_H_ */
```

- [ ] **Step 5: Create `port/port_stm32.c`**

```c
/**
 * @file       port_stm32.c
 * @brief      STM32 HAL port of the regmap library: tick, critical section, callbacks
 *
 * Port file: written once by `regmap export-lib`, then owned by the project.
 */

#include "port.h"
#include "port_stm32.h"

static uint32_t criticalPrimask;
static uint32_t criticalNesting;

uint32_t Port_GetTickMs(void)
{
  return HAL_GetTick();
}


void Port_CriticalEnter(void)
{
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  if (criticalNesting++ == 0u)
  {
    criticalPrimask = primask;
  }
}


void Port_CriticalExit(void)
{
  if (criticalNesting > 0u && --criticalNesting == 0u)
  {
    __set_PRIMASK(criticalPrimask);
  }
}

#if REGMAP_LIB_MODBUS
void MbPort_NotifyFromIsr(void)
{
  /* FreeRTOS: wake the task calling MbSlave_Handle, e.g. vTaskNotifyGiveFromISR(...) */
}
#endif

#if REGMAP_LIB_LEBIN
void LebinPort_NotifyFromIsr(void)
{
  /* FreeRTOS: wake the task calling Lebin_Handle, e.g. vTaskNotifyGiveFromISR(...) */
}
#endif


void PortStm32_UartRxEvent(UART_HandleTypeDef *huart, uint16_t size)
{
#if REGMAP_LIB_MODBUS
  MbPortStm32_RxEvent(huart, size);
#endif
#if REGMAP_LIB_LEBIN && (LEBIN_PORT_TRANSPORT == LEBIN_PORT_UART)
  LebinPortUart_RxEvent(huart, size);
#endif
  UNUSED(huart);
  UNUSED(size);
}


void PortStm32_UartTxCplt(UART_HandleTypeDef *huart)
{
#if REGMAP_LIB_MODBUS
  MbPortStm32_TxCplt(huart);
#endif
  UNUSED(huart);
}


void PortStm32_UartError(UART_HandleTypeDef *huart)
{
#if REGMAP_LIB_MODBUS
  MbPortStm32_Error(huart);
#endif
#if REGMAP_LIB_LEBIN && (LEBIN_PORT_TRANSPORT == LEBIN_PORT_UART)
  LebinPortUart_Error(huart);
#endif
  UNUSED(huart);
}

#if REGMAP_LIB_HAL_UART_CALLBACKS
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
  PortStm32_UartRxEvent(huart, Size);
}


void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  PortStm32_UartTxCplt(huart);
}


void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  PortStm32_UartError(huart);
}
#endif
```

- [ ] **Step 6: Create `port/modbus_port_stm32.c`**

```c
/**
 * @file       modbus_port_stm32.c
 * @brief      Modbus RTU slave port for the STM32 HAL UART
 *
 * Port file: written once by `regmap export-lib`, then owned by the project.
 * The UART (pins, clock, NVIC, optional DMA) is configured in CubeMX; this file only changes
 * baud rate, parity and stop bits and handles reception and RS-485 direction.
 */

#include "port.h"
#include "port_stm32.h"

#if REGMAP_LIB_MODBUS

extern UART_HandleTypeDef MB_PORT_UART_HANDLE;

/** Reception buffer of one idle-line chunk */
static uint8_t mbRxBuffer[MBSLAVE_FRAME_MAX];

static void MbPortStm32_DirReceive(void)
{
#if defined(MB_PORT_DE_GPIO_PORT) && !defined(MB_PORT_HW_DE)
  HAL_GPIO_WritePin(MB_PORT_DE_GPIO_PORT, MB_PORT_DE_GPIO_PIN, GPIO_PIN_RESET);
#endif
}


static void MbPortStm32_DirTransmit(void)
{
#if defined(MB_PORT_DE_GPIO_PORT) && !defined(MB_PORT_HW_DE)
  HAL_GPIO_WritePin(MB_PORT_DE_GPIO_PORT, MB_PORT_DE_GPIO_PIN, GPIO_PIN_SET);
#endif
}


static void MbPortStm32_StartReceive(void)
{
  MbPortStm32_DirReceive();
#if MB_PORT_USE_DMA
  if (HAL_UARTEx_ReceiveToIdle_DMA(&MB_PORT_UART_HANDLE, mbRxBuffer, sizeof(mbRxBuffer)) == HAL_OK)
  {
    /* Only idle-line and buffer-full events are wanted */
    __HAL_DMA_DISABLE_IT(MB_PORT_UART_HANDLE.hdmarx, DMA_IT_HT);
  }
#else
  (void)HAL_UARTEx_ReceiveToIdle_IT(&MB_PORT_UART_HANDLE, mbRxBuffer, sizeof(mbRxBuffer));
#endif
}


Status_t MbPort_Init(const MbSlave_Config_t *cfg)
{
  UART_HandleTypeDef *huart = &MB_PORT_UART_HANDLE;
  HAL_StatusTypeDef status;

  (void)HAL_UART_Abort(huart);

  huart->Init.BaudRate = cfg->baudRate;
  huart->Init.StopBits = (cfg->stopBits == 2u) ? UART_STOPBITS_2 : UART_STOPBITS_1;
  switch (cfg->parity)
  {
    case MBSLAVE_PARITY_EVEN:
      huart->Init.Parity = UART_PARITY_EVEN;
      huart->Init.WordLength = UART_WORDLENGTH_9B; /* 8 data bits + parity */
      break;
    case MBSLAVE_PARITY_ODD:
      huart->Init.Parity = UART_PARITY_ODD;
      huart->Init.WordLength = UART_WORDLENGTH_9B;
      break;
    default:
      huart->Init.Parity = UART_PARITY_NONE;
      huart->Init.WordLength = UART_WORDLENGTH_8B;
      break;
  }

#if defined(MB_PORT_HW_DE)
  status = HAL_RS485Ex_Init(huart, UART_DE_POLARITY_HIGH, 0, 0);
#else
  status = HAL_UART_Init(huart);
#endif
  if (status != HAL_OK)
  {
    return STATUS_ERROR;
  }

  MbPortStm32_StartReceive();
  return STATUS_OK;
}


Status_t MbPort_Send(const uint8_t *data, uint16_t len)
{
  HAL_StatusTypeDef status;

  (void)HAL_UART_AbortReceive(&MB_PORT_UART_HANDLE);
  MbPortStm32_DirTransmit();
#if MB_PORT_USE_DMA
  status = HAL_UART_Transmit_DMA(&MB_PORT_UART_HANDLE, (uint8_t *)data, len);
#else
  status = HAL_UART_Transmit_IT(&MB_PORT_UART_HANDLE, (uint8_t *)data, len);
#endif
  if (status != HAL_OK)
  {
    MbPortStm32_StartReceive();
    return STATUS_ERROR;
  }

  return STATUS_OK;
}


void MbPortStm32_RxEvent(UART_HandleTypeDef *huart, uint16_t size)
{
  if (huart != &MB_PORT_UART_HANDLE)
  {
    return;
  }
  if (size > 0u)
  {
    MbSlave_RxFrame(mbRxBuffer, size);
  }
  MbPortStm32_StartReceive();
}


void MbPortStm32_TxCplt(UART_HandleTypeDef *huart)
{
  if (huart != &MB_PORT_UART_HANDLE)
  {
    return;
  }
  /* Transmission complete (TC): the last stop bit has left the line */
  MbPortStm32_StartReceive();
  MbSlave_TxDone();
}


void MbPortStm32_Error(UART_HandleTypeDef *huart)
{
  if (huart != &MB_PORT_UART_HANDLE)
  {
    return;
  }
  (void)HAL_UART_AbortReceive(huart);
  MbPortStm32_StartReceive();
}

#endif /* REGMAP_LIB_MODBUS */
```

- [ ] **Step 7: Create `port/lebin_port_usb_cdc.c`**

```c
/**
 * @file       lebin_port_usb_cdc.c
 * @brief      LeBin port for the ST USB Device Library (CDC class)
 *
 * Port file: written once by `regmap export-lib`, then owned by the project.
 * In the CubeMX usbd_cdc_if.c, include "port_stm32.h" and make CDC_Receive_FS (or _HS) call
 * LebinPortUsb_Receive(Buf, *Len) and return USBD_OK (see doc/firmware-lib.md).
 */

#include "port.h"
#include "port_stm32.h"

#if REGMAP_LIB_LEBIN && (LEBIN_PORT_TRANSPORT == LEBIN_PORT_USB_CDC)

#include "lebin.h"
#include "usbd_cdc.h"

extern USBD_HandleTypeDef LEBIN_PORT_USB_DEVICE;

static USBD_CDC_HandleTypeDef *LebinPortUsb_Cdc(void)
{
  return (USBD_CDC_HandleTypeDef *)LEBIN_PORT_USB_DEVICE.pClassData;
}


Status_t LebinPort_Init(void)
{
  return STATUS_OK;
}


bool LebinPort_TxReady(void)
{
  USBD_CDC_HandleTypeDef *cdc = LebinPortUsb_Cdc();

  /* Not connected: responses are dropped by LebinPort_Send */
  return (cdc == NULL) || (cdc->TxState == 0u);
}


Status_t LebinPort_Send(const uint8_t *data, uint16_t len)
{
  USBD_CDC_HandleTypeDef *cdc = LebinPortUsb_Cdc();

  if (cdc == NULL)
  {
    return STATUS_ERROR;
  }
  if (cdc->TxState != 0u)
  {
    return STATUS_BUSY;
  }
  (void)USBD_CDC_SetTxBuffer(&LEBIN_PORT_USB_DEVICE, (uint8_t *)data, len);

  return (USBD_CDC_TransmitPacket(&LEBIN_PORT_USB_DEVICE) == USBD_OK) ? STATUS_OK : STATUS_BUSY;
}


void LebinPortUsb_Receive(uint8_t *buf, uint32_t len)
{
  /* The library copies the data, so the USB buffer can receive the next packet at once */
  Lebin_RxBytes(buf, len);
  (void)USBD_CDC_SetRxBuffer(&LEBIN_PORT_USB_DEVICE, buf);
  (void)USBD_CDC_ReceivePacket(&LEBIN_PORT_USB_DEVICE);
}

#endif
```

- [ ] **Step 8: Create `port/lebin_port_uart.c`**

```c
/**
 * @file       lebin_port_uart.c
 * @brief      LeBin port for the STM32 HAL UART
 *
 * Port file: written once by `regmap export-lib`, then owned by the project. Used when
 * LEBIN_PORT_TRANSPORT is LEBIN_PORT_UART; the UART is configured in CubeMX (IRQ enabled).
 */

#include "port.h"
#include "port_stm32.h"

#if REGMAP_LIB_LEBIN && (LEBIN_PORT_TRANSPORT == LEBIN_PORT_UART)

#include "lebin.h"

extern UART_HandleTypeDef LEBIN_PORT_UART_HANDLE;

static uint8_t lebinRxBuffer[64];

static void LebinPortUart_StartReceive(void)
{
  (void)HAL_UARTEx_ReceiveToIdle_IT(&LEBIN_PORT_UART_HANDLE, lebinRxBuffer,
                                    sizeof(lebinRxBuffer));
}


Status_t LebinPort_Init(void)
{
  LebinPortUart_StartReceive();
  return STATUS_OK;
}


bool LebinPort_TxReady(void)
{
  return LEBIN_PORT_UART_HANDLE.gState == HAL_UART_STATE_READY;
}


Status_t LebinPort_Send(const uint8_t *data, uint16_t len)
{
  return (HAL_UART_Transmit_IT(&LEBIN_PORT_UART_HANDLE, (uint8_t *)data, len) == HAL_OK)
         ? STATUS_OK : STATUS_BUSY;
}


void LebinPortUart_RxEvent(UART_HandleTypeDef *huart, uint16_t size)
{
  if (huart != &LEBIN_PORT_UART_HANDLE)
  {
    return;
  }
  if (size > 0u)
  {
    Lebin_RxBytes(lebinRxBuffer, size);
  }
  LebinPortUart_StartReceive();
}


void LebinPortUart_Error(UART_HandleTypeDef *huart)
{
  if (huart != &LEBIN_PORT_UART_HANDLE)
  {
    return;
  }
  (void)HAL_UART_AbortReceive(huart);
  LebinPortUart_StartReceive();
}

#endif
```

- [ ] **Step 9: Create `port/upgrade_port_stm32.c`**

```c
/**
 * @file       upgrade_port_stm32.c
 * @brief      Firmware upgrade port: STM32 internal flash
 *
 * Port file: written once by `regmap export-lib`, then owned by the project.
 * Implemented for STM32H7 (sectors, 256-bit flash words) and for page-based families with
 * double-word programming and a page index (G0, G4, L4, L5, U5 ...). Other families: adapt
 * UpgradePortStm32_EraseUnit and UpgradePortStm32_Program (doc/firmware-porting.md).
 */

#include "port.h"

#if REGMAP_LIB_UPGRADE

#include "configuration.h"

#if defined(FLASH_TYPEPROGRAM_FLASHWORD)
/* STM32H7 */
#define UPGRADE_PORT_ERASE_UNIT     FLASH_SECTOR_SIZE
#define UPGRADE_PORT_PROGRAM_SIZE   (FLASH_NB_32BITWORD_IN_FLASHWORD * 4u)
#elif defined(FLASH_TYPEERASE_PAGES) && defined(FLASH_PAGE_SIZE) \
      && defined(FLASH_TYPEPROGRAM_DOUBLEWORD)
/* Page-based families */
#define UPGRADE_PORT_ERASE_UNIT     FLASH_PAGE_SIZE
#define UPGRADE_PORT_PROGRAM_SIZE   8u
#else
#error "Implement flash erase/program for this STM32 family (see doc/firmware-porting.md)"
#endif

/** Area [0, erasedEnd) of the upgrade area is erased */
static uint32_t erasedEnd;

/** Aligned copy of one programming unit (the source may be unaligned) */
static uint8_t programBuffer[UPGRADE_PORT_PROGRAM_SIZE] __attribute__((aligned(8)));

static Status_t UpgradePortStm32_EraseUnit(uint32_t address)
{
  FLASH_EraseInitTypeDef erase;
  uint32_t error = 0;

  memset(&erase, 0, sizeof(erase));
#if defined(FLASH_TYPEPROGRAM_FLASHWORD)
  erase.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase.NbSectors = 1;
  erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
#if defined(DUAL_BANK)
  if (address >= FLASH_BANK2_BASE)
  {
    erase.Banks = FLASH_BANK_2;
    erase.Sector = (address - FLASH_BANK2_BASE) / FLASH_SECTOR_SIZE;
  }
  else
#endif
  {
    erase.Banks = FLASH_BANK_1;
    erase.Sector = (address - FLASH_BANK1_BASE) / FLASH_SECTOR_SIZE;
  }
#else
  erase.TypeErase = FLASH_TYPEERASE_PAGES;
  erase.NbPages = 1;
#if defined(FLASH_BANK_2) && defined(FLASH_BANK_SIZE)
  erase.Banks = ((address - FLASH_BASE) >= FLASH_BANK_SIZE) ? FLASH_BANK_2 : FLASH_BANK_1;
  erase.Page = ((address - FLASH_BASE) % FLASH_BANK_SIZE) / FLASH_PAGE_SIZE;
#else
  erase.Banks = FLASH_BANK_1;
  erase.Page = (address - FLASH_BASE) / FLASH_PAGE_SIZE;
#endif
#endif

  return (HAL_FLASHEx_Erase(&erase, &error) == HAL_OK) ? STATUS_OK : STATUS_ERROR;
}


static Status_t UpgradePortStm32_Program(uint32_t address, const uint8_t *data)
{
  HAL_StatusTypeDef status;

  memcpy(programBuffer, data, UPGRADE_PORT_PROGRAM_SIZE);
#if defined(FLASH_TYPEPROGRAM_FLASHWORD)
  status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, address,
                             (uint32_t)(uintptr_t)programBuffer);
#else
  {
    uint64_t value;

    memcpy(&value, programBuffer, sizeof(value));
    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, value);
  }
#endif

  return (status == HAL_OK) ? STATUS_OK : STATUS_ERROR;
}


/** Erase erase units until [0, end) is erased */
static Status_t UpgradePortStm32_EnsureErased(uint32_t end)
{
  Status_t ret = STATUS_OK;

  (void)HAL_FLASH_Unlock();
  while (ret == STATUS_OK && erasedEnd < end)
  {
    ret = UpgradePortStm32_EraseUnit(UPGRADE_PORT_AREA_ADDRESS + erasedEnd);
    erasedEnd += UPGRADE_PORT_ERASE_UNIT;
  }
  (void)HAL_FLASH_Lock();

  return ret;
}


Status_t UpgradePort_Begin(uint32_t size)
{
  erasedEnd = 0;
  if (size > UPGRADE_PORT_AREA_SIZE)
  {
    return STATUS_ERROR;
  }

  return (size > 0u) ? UpgradePortStm32_EnsureErased(size) : STATUS_OK;
}


Status_t UpgradePort_Write(uint32_t offset, const uint8_t *data, uint32_t len)
{
  Status_t ret;
  uint32_t i;

  if (offset % UPGRADE_PORT_PROGRAM_SIZE != 0u || len % UPGRADE_PORT_PROGRAM_SIZE != 0u
      || offset + len > UPGRADE_PORT_AREA_SIZE)
  {
    return STATUS_ERROR;
  }

  ret = UpgradePortStm32_EnsureErased(offset + len);

  (void)HAL_FLASH_Unlock();
  for (i = 0; ret == STATUS_OK && i < len; i += UPGRADE_PORT_PROGRAM_SIZE)
  {
    ret = UpgradePortStm32_Program(UPGRADE_PORT_AREA_ADDRESS + offset + i, data + i);
  }
  (void)HAL_FLASH_Lock();

  return ret;
}


Status_t UpgradePort_Verify(uint32_t size)
{
  UNUSED(size);
  return UPGRADE_PORT_VERIFY(UPGRADE_PORT_AREA_ADDRESS, size);
}


void UpgradePort_Apply(void)
{
  /* Request the switch to the new image here, e.g. set a flag for a restart into the
   * bootloader after the Modbus response has been sent. */
}

#endif /* REGMAP_LIB_UPGRADE */
```

- [ ] **Step 10: Run tests to verify they pass**

Run: `pytest tests/test_firmware_ports.py tests/test_firmware_core.py tests/test_export_lib.py -q`
Expected: PASS. A failure in the stub check means a typo or type error in a port file — fix the port, not the stub, unless the stub contradicts the real HAL signature.

- [ ] **Step 11: Lint and commit**

```bash
ruff check . && ruff format --check .
git add src/regmap/firmware_lib/port tests/c/hal_stub tests/test_firmware_ports.py
git commit -m "feat: STM32 HAL ports of the firmware library"
```

---

### Task 7: Documentation

**Files:**
- Create: `doc/firmware-lib.md`, `doc/firmware-porting.md`
- Modify: `doc/cli.md`, `doc/index.md`, `doc/development.md`, `README.md`
- Modify: `doc/design/specs/2026-10-08-firmware-lib-design.md` (status line)

**Interfaces:** none (docs only).

- [ ] **Step 1: Create `doc/firmware-lib.md`**

````markdown
# Firmware communication library

`regmap export-lib DIR` writes C sources that connect the generated register map to the
outside world:

- **LeBin** — register read/write and firmware upgrade over USB CDC or UART,
- **Modbus RTU slave** — function codes 3, 4, 6, 16 over UART/RS-485, firmware upgrade over
  holding registers,
- **register access** — `configuration.c/h` (`CONF_PTR`, `CONF_INT`, flash streams, ...).

Together with `regmap generate` (`reg_map.*`, `mb_rtu_app.*`) the set compiles on any STM32
family with the STM32Cube HAL.

## Core and port files

| kind | files | owner |
|---|---|---|
| core | `lebin.*`, `modbus_slave.*`, `mb_upgrade.*`, `fw_upgrade.*`, `configuration.*`, `port.h`, `lib_bytes.h` | regmap — do not edit |
| port | `regmap_lib_conf.h`, `common.h`, `system_msp.h`, `config_app.*`, `port_stm32.*`, `modbus_port_stm32.c`, `lebin_port_usb_cdc.c`, `lebin_port_uart.c`, `upgrade_port_stm32.c` | the project — edit freely |

Core files carry a `@regmap-lib <version> sha256:<hash>` line. Re-running `export-lib`:

| file in DIR | core file | port file |
|---|---|---|
| missing | written | written |
| unchanged since export | updated to the new version | kept |
| edited | **error, nothing written** (`--force` overwrites) | kept (`--force-ports` overwrites) |

Output per file: `written`, `updated`, `kept` or `unchanged`. Exit code 1 when a core file
was edited by hand.

## Integration into a CubeMX project

1. Generate and export, e.g. into `Core/RegMap`, and add the directory to the include path
   and the sources:

   ```sh
   regmap generate Documents/mydevice.yaml --out Core/RegMap
   regmap export-lib Core/RegMap
   ```

   If the project already has `common.h` or `system_msp.h`, move their content into the
   exported files (re-export keeps them).

2. **CubeMX**
   - Modbus UART: asynchronous mode, global interrupt enabled (optionally RX/TX DMA, normal
     mode). RS-485 driver enable: a GPIO output, or the USART hardware DE pin.
   - LeBin over USB: USB device, Communication Device Class. Over UART: like Modbus.
   - The HAL must provide `HAL_UARTEx_ReceiveToIdle_IT/DMA` (STM32Cube packages from 2021 on).

3. **`regmap_lib_conf.h`**: enable modules, set the CubeMX handle names
   (`MB_PORT_UART_HANDLE`, `LEBIN_PORT_USB_DEVICE`, `LEBIN_PORT_UART_HANDLE`), the DE pin
   (`MB_PORT_DE_GPIO_PORT/PIN`, or `MB_PORT_HW_DE`), `MB_PORT_USE_DMA` and the LeBin transport.
   With DMA on STM32H7 the buffers must lie in DMA-accessible RAM (not DTCM).

4. **USB CDC**: in `USB_DEVICE/App/usbd_cdc_if.c`:

   ```c
   /* USER CODE BEGIN INCLUDE */
   #include "port_stm32.h"
   /* USER CODE END INCLUDE */

   static int8_t CDC_Receive_FS(uint8_t* Buf, uint32_t *Len)
   {
     /* USER CODE BEGIN 6 */
     LebinPortUsb_Receive(Buf, *Len);
     return (USBD_OK);
     /* USER CODE END 6 */
   }
   ```

5. **HAL UART callbacks**: `port_stm32.c` defines `HAL_UARTEx_RxEventCallback`,
   `HAL_UART_TxCpltCallback` and `HAL_UART_ErrorCallback`. If the project needs them for other
   UARTs, set `REGMAP_LIB_HAL_UART_CALLBACKS` to 0 and call `PortStm32_UartRxEvent`,
   `PortStm32_UartTxCplt` and `PortStm32_UartError` from your callbacks.

6. **Application** (bare-metal):

   ```c
   #include "configuration.h"
   #include "config_app.h"
   #include "lebin.h"
   #include "modbus_slave.h"

   MbSlave_Config_t mb;

   if (Config_Init() != STATUS_OK) { Error_Handler(); }   /* reg_map.h layout mismatch */
   ConfigApp_ModbusConfig(&mb);
   MbSlave_Init(&mb);
   Lebin_Init();

   while (1)
   {
     MbSlave_Handle();
     Lebin_Handle();
   }
   ```

   FreeRTOS: call `MbSlave_Handle()` / `Lebin_Handle()` from a task loop with a short delay,
   or wake the task from `MbPort_NotifyFromIsr` / `LebinPort_NotifyFromIsr` in
   `port_stm32.c` (e.g. `vTaskNotifyGiveFromISR`) and block on `ulTaskNotifyTake` with a
   timeout of a few milliseconds (the timeouts of both protocols need periodic calls).

7. **`config_app.c`**: `Config_AppInit` (restore stored registers), `Config_Callback(id)`
   (react to written registers, store flash registers), `ConfigApp_ModbusConfig` (Modbus
   settings from the COM registers of the `regmap init` starter map) and
   `MbSlave_TimeoutChanged` (sets `STAT_BIT_MB_TIMEOUT`). Writing 1 to `MB_APPLY` applies the
   Modbus settings after the response has been sent.

8. **Firmware upgrade** (`REGMAP_LIB_UPGRADE`): the image is written to the area
   `UPGRADE_PORT_AREA_ADDRESS` / `UPGRADE_PORT_AREA_SIZE`, by default the linker symbols
   `_LD_ADDRESS_BUFFER_APP` and `_LD_SIZE_BUFFER_APP`. Set `UPGRADE_PORT_VERIFY` to your image
   check and fill `UpgradePort_Apply` (Modbus mode 2). The linker script must also define
   `_LD_FW_INFO_OFFSET`, `_LD_ADDRESS_BOOTLOADER`, `_LD_ADDRESS_APPLICATION`,
   `_LD_ADDRESS_CALIBRATION` if the application uses them, and the sections `.sectionFwInfo`
   and `.sectionEndOfFlash`.

## Protocols

### LeBin

Packet: `0x90`, packet ID, total length (16 bit little-endian, header included), payload.

| ID | request | response |
|---|---|---|
| 130 read | IDs (4 bytes each) | 5: ID + value for each known ID |
| 129 write | ID + value pairs | 5: ID + stored value; `0xF0` bytes for an unknown ID |
| 255 FW upgrade | offset (4 bytes) + data (multiple of 32 bytes; none = end) | 127: offset + length, code 0 OK / 1 error / 2 wrong length |

Unknown packet IDs get no response. Garbage before a start byte is skipped; a partial packet
without new bytes for 750 ms (`LEBIN_NEW_PACKET_MS`) is dropped. `Lebin_GetErrorCount()`
counts dropped data.

### Modbus RTU

Function codes 3, 4 (count 1–125), 6 and 16 (count 1–123); others return exception 01,
addresses outside the map exception 02, wrong counts exception 03. A frame ends at a UART idle
line; a frame failing the CRC waits up to t3.5 (at least 2 ms) for the rest. Broadcast
(address 0) is processed without a response.

Firmware upgrade registers (holding, wire compatible with the former `mb_upgrade.c`):

| register | content |
|---|---|
| 1000 | type |
| 1001 | mode: 0 erase on the fly, 1 erase at start, 2 apply |
| 1002–1003 | image size (low word first) |
| 1004 | page size in bytes (≤ 64) |
| 1005–1006 | page offset (low word first) |
| 1007–1038 | page data |
| 1039 | status: 0 busy, 1 ready, 2 done OK, 3 done error |
| 1040 | write done (write 1 after each page) |
| 1041 | reserved |

## Porting

See [firmware-porting.md](firmware-porting.md).
````

- [ ] **Step 2: Create `doc/firmware-porting.md`**

````markdown
# Porting the firmware library

The core (`lebin.c`, `modbus_slave.c`, `mb_upgrade.c`, `fw_upgrade.c`, `configuration.c`) is
plain C99 and calls only the functions in `port.h`. Everything MCU-specific is in the port
files, which belong to the project.

## Port contract (`port.h`)

| function | called from | must do |
|---|---|---|
| `Port_GetTickMs` | main/task, ISR | millisecond tick, wrapping |
| `Port_CriticalEnter/Exit` | main/task | disable/restore interrupts, nestable |
| `MbPort_Init(cfg)` | main/task | stop transfers, set baud/parity/stop bits, start reception |
| `MbPort_Send(data, len)` | main/task | DE on, start transmission; later call `MbSlave_TxDone()` from the ISR when the last bit is out, DE off, restart reception |
| `MbPort_NotifyFromIsr` | ISR | optional task wake-up |
| `LebinPort_Init` | main/task | start reception |
| `LebinPort_TxReady` | main/task | previous response handed over completely |
| `LebinPort_Send(data, len)` | main/task | start transmission; `data` stays valid until `TxReady` |
| `LebinPort_NotifyFromIsr` | ISR | optional task wake-up |
| `UpgradePort_Begin(size)` | main/task | reset erase tracking; size > 0: erase the whole size |
| `UpgradePort_Write(offset, data, len)` | main/task | erase units the write enters, program (`data` may be unaligned) |
| `UpgradePort_Verify(size)` | main/task | check the image |
| `UpgradePort_Apply` | main/task | switch to the new image (Modbus mode 2) |

Reception: call `MbSlave_RxFrame(data, len)` with every chunk received up to an idle line,
and `Lebin_RxBytes(data, len)` with every received chunk. Both copy the data.

## Another STM32 family

- **UART**: usually nothing — the ports use only HAL calls available on all families. Older
  HAL versions without `HAL_UARTEx_ReceiveToIdle_*` need an update of the Cube package.
- **USB**: `lebin_port_usb_cdc.c` targets the classic ST USB Device Library. For USBX or
  TinyUSB, write `LebinPort_Init/TxReady/Send` against that stack and call `Lebin_RxBytes`
  from its receive callback.
- **Flash** (`upgrade_port_stm32.c`): implemented for STM32H7 and for page-based families with
  a page index and double-word programming. F0/F1/F3 (`PageAddress`), F2/F4/F7 (non-uniform
  sectors), L0/L1 (word programming), H5/U5 (quad-word) need their own
  `UpgradePortStm32_EraseUnit`, `UpgradePortStm32_Program` and `UPGRADE_PORT_ERASE_UNIT` /
  `UPGRADE_PORT_PROGRAM_SIZE`. The upgrade area must start on an erase-unit boundary; on H7
  invalidate the D-cache before verifying if the area is cached.

## Another MCU vendor

Replace `common.h` (keep `Status_t`, `STATUS_*`, `UNUSED`, `__packed`, `__aligned`,
`SAT_UP/DOWN`), drop the `*_stm32.c` files and implement `port.h`. The generated
`mb_rtu_app.c` uses the CMSIS `__REV16`; provide it for non-Cortex-M targets.

## Testing a port change

`pytest tests/test_firmware_core.py` builds the core with the host `gcc` against the fake port
in `tests/c/`; `pytest tests/test_firmware_ports.py` syntax-checks the STM32 ports against the
HAL stub in `tests/c/hal_stub/`.
````

- [ ] **Step 3: Update `doc/cli.md`**

Replace the command synopsis block with:

```
regmap init NAME [-o MAP.yaml] [--force]
regmap generate MAP.yaml [--out DIR] [--allow-id-change] [--check]
regmap import-xlsx WORKBOOK.xlsm [-o MAP.yaml] [--force]
regmap export-lib DIR [--force] [--force-ports]
regmap schema [-o FILE]
regmap --version
```

Insert before `## \`schema\``:

```markdown
## `export-lib`

Writes the firmware communication library (LeBin, Modbus RTU slave, firmware upgrade,
register access and STM32 ports) into `DIR`, see [firmware-lib.md](firmware-lib.md). Core
files are updated on re-export unless they were edited by hand (then nothing is written and
the exit code is 1; `--force` overwrites them). Port and configuration files are written only
when missing (`--force-ports` overwrites them).
```

In the exit-code table change the row for 1 to:

```
| 1 | invalid map, template error, breaking change, `--check` found differences, import failed, library file edited by hand |
```

- [ ] **Step 4: Update `doc/index.md`, `doc/development.md`, `README.md`**

`doc/index.md`: add after the `cli.md` row:

```
| [firmware-lib.md](firmware-lib.md) | Firmware library from `regmap export-lib`: LeBin, Modbus RTU slave, upgrade, integration. |
| [firmware-porting.md](firmware-porting.md) | Port contract and porting to other MCU families. |
```

`doc/development.md`, project layout table: add after the `src/regmap/importers/` row:

```
| `src/regmap/export_lib.py` | `regmap export-lib`: stamps and writes the firmware library. |
| `src/regmap/firmware_lib/` | Firmware library sources: `core/` (hash-protected) and `port/` (owned by the project). |
```

`doc/development.md`, Tests section: add the bullet:

```markdown
- `tests/test_firmware_core.py` exports the firmware library, generates `example/vms1511.yaml`
  and builds the core with the host `gcc` against the fake port in `tests/c/`, then runs the C
  tests; `tests/test_firmware_ports.py` syntax-checks the STM32 ports against
  `tests/c/hal_stub/`. Both are skipped without `gcc`.
```

`README.md`, Usage section: add after the `regmap generate` block:

````markdown
Add the firmware communication library (LeBin, Modbus RTU slave) to the firmware project:

```sh
regmap export-lib Core/RegMap
```
````

and in the Documentation paragraph add `[firmware library](doc/firmware-lib.md),` after
`[outputs](doc/outputs.md),`.

Spec: change `- **Status:** Draft, awaiting review` to `- **Status:** Approved (2026-10-08), implemented`.

- [ ] **Step 5: Verify and commit**

```bash
pytest -q && ruff check . && ruff format --check .
git add doc README.md
git commit -m "docs: firmware communication library"
```

---

### Task 8: Verification on a target and clean-up

**Files:** none in the repository (the human partner's firmware project); possibly delete the untracked `firmware/` directory.

- [ ] **Step 1: Full local check**

Run: `pytest -q && ruff check . && ruff format --check .`
Expected: all pass, no skipped firmware tests on a machine with `gcc`.

- [ ] **Step 2: Push and check CI**

```bash
git push
gh run watch
```

Expected: CI green on Python 3.12–3.14 (the firmware tests run there with Ubuntu's `gcc`).

- [ ] **Step 3: Target build (human partner)**

Ask the human partner to export the library into one real STM32H7 project, follow
`doc/firmware-lib.md`, build in STM32CubeIDE and test with the PC tools: LeBin read/write over
USB, Modbus read/write and `MB_APPLY`, one firmware upgrade over each protocol. Fix reported
problems in the library with a test first where the host tests can reproduce them.

- [ ] **Step 4: Decide about `firmware/`**

Ask the human partner whether to delete the untracked `firmware/` directory (the original
sources) now that the library replaces it (spec §10). Delete only after an explicit yes.
````
