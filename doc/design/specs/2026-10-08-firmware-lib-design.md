# Firmware Communication Library (`regmap export-lib`): Design

- **Date:** 2026-10-08
- **Status:** Approved (2026-10-08), implemented
- **Related:** [py-reg-map design](2026-09-28-py-reg-map-design.md)

## 1. Context

`regmap generate` produces `reg_map.c/h` and `mb_rtu_app.c/h`, but a firmware also needs the
communication code that uses them: the LeBin protocol (usually over USB CDC, sometimes UART) and
a Modbus RTU slave (always UART, RS-485). Today this code is copied between projects by hand.
The copies in `firmware/` (input of this work, not committed) show the problems:

- STM32 HAL calls, register access (`ISR`/`RDR`/`CR1`, USART v2 only), peripherals and pins
  (USART2, TIM3, PA1–PA3) are hard-coded in the protocol code.
- USB glue is a modified CubeMX `usbd_cdc_if.c` with `hUsbDeviceHS` hard-coded.
- Product-specific code: 128 KB flash sectors, H7 flash-size address `0x1FF1E880`,
  `version < 34` check, IRQ priorities and timer assignment in `common.h`,
  `#include "stm32h7xx_hal.h"`.
- Files come from different projects (`PRIO_IRQ_MODBUS` used but not defined in `common.h`).
- Bugs (see §8).

## 2. Goals

1. A new command `regmap export-lib <dir>` writes a complete, compilable set of firmware sources
   that together with `regmap generate` outputs builds on any STM32 family supported by STM32 HAL.
2. Protocol logic (**core**) is plain C99 without HAL; everything MCU-specific lives in a small
   number of **port** files that the project edits.
3. Re-running the export updates the core but never overwrites the project's port edits.
4. Works bare-metal (super-loop) and under FreeRTOS.
5. Wire compatibility: LeBin packets, Modbus register map and Modbus upgrade registers stay
   unchanged, so existing PC software keeps working.
6. The core is unit-tested on the host.

## 3. Non-goals

- USBX / new ST USB device stack, TinyUSB (port can be written later against `port.h`).
- USART receiver timeout (RTOR) based framing.
- LeBin event request (packet 131) — dropped, the event buffer was never filled.
- Modbus FC 1/2/5/15 (coils, discrete inputs) and multiple Modbus/LeBin instances.
- Compiling the STM32 ports in CI.

## 4. Exported files

### 4.1 Core (managed, updated by re-export)

| File | Purpose | Origin |
|---|---|---|
| `lebin.c/h` | LeBin: byte stream → packets (ring buffer, 750 ms new-packet timeout), read/write registers, FW upgrade packets | `com_proto.*` + logic from `usbd_cdc_if.c` |
| `modbus_slave.c/h` | Modbus RTU slave: frame check, CRC16, FC 3/4/6/16, exceptions, communication timeout | `modbus_slave.*` |
| `mb_upgrade.c/h` | Modbus upgrade via holding registers from address 1000 (handshake) | `mb_upgrade.*` |
| `fw_upgrade.c/h` | Common upgrade backend used by LeBin and Modbus upgrade | new |
| `configuration.c/h` | Register access (`CONF_PTR`, `CONF_INT`, …, `Config_CheckLimits`, `Config_ApplyConfig`, flash streams, sync) | `configuration.*`, product bits removed |
| `port.h` | Interface the port implements (§5.2); changes together with the core | new |
| `lib_bytes.h` | Unaligned-safe little/big-endian read/write helpers | new |

### 4.2 Ports and configuration (written once, owned by the project)

| File | Purpose |
|---|---|
| `regmap_lib_conf.h` | Module switches (`REGMAP_LIB_LEBIN`, `REGMAP_LIB_MODBUS`, `REGMAP_LIB_UPGRADE`), buffer sizes, CubeMX UART/USB handle names, DE pin, LeBin transport |
| `common.h` | `Status_t`, `STATUS_*`, `MIN/MAX`, `SAT_UP/DOWN`, `__weak`, `__packed`, `UNUSED`; includes `main.h` instead of a family HAL header; no product IRQ/timer tables |
| `system_msp.h` | Minimal stand-in (the generated `mb_rtu_app.c` includes it); delete when the project has its own |
| `port_stm32.c/h` | `Port_GetTickMs`, critical section (PRIMASK), RTOS notify hooks, HAL UART callback dispatch |
| `modbus_port_stm32.c` | HAL UART with `HAL_UARTEx_ReceiveToIdle_IT/DMA`, RS-485 DE pin, baud/parity/stop mapping |
| `lebin_port_usb_cdc.c` | LeBin over ST USB Device Library CDC; FS/HS handle by macro; plus a snippet for the CubeMX `usbd_cdc_if.c` `USER CODE` section |
| `lebin_port_uart.c` | LeBin over UART (`ReceiveToIdle`) |
| `upgrade_port_stm32.c` | Flash erase (on the fly or at start) and program for H7 and page-based families, image verification hook |
| `config_app.c/h` | Application hooks: `Config_AppInit`, `Config_Callback(id)`, Modbus settings mapping (`conf.com.mb_*` → `MbSlave_Config_t`, `STAT_BIT_MB_TIMEOUT`) |

UART pins, clocks, IRQs and DMA are configured in CubeMX; the ports only use the CubeMX
handles and re-initialise the UART for baud rate/parity/stop bits. The CubeMX-generated
`usbd_cdc_if.c` is not replaced; the project pastes one call into its `CDC_Receive_xS` user
section (documented in `doc/firmware-lib.md`).

### 4.3 Dependencies

Core depends only on: `common.h`, `regmap_lib_conf.h`, `port.h`, generated `reg_map.h` and
`mb_rtu_app.h`, `<string.h>`, `<stdint.h>`. `configuration.c` additionally uses linker symbols
(`_LD_FW_INFO_OFFSET`, `_LD_ADDRESS_*`) and sections `.sectionFwInfo` / `.sectionEndOfFlash`;
these are documented. The Modbus core does not reference register names from the YAML — the
project maps its registers in `config_app.c`.

## 5. Architecture

### 5.1 Data flow

```
Modbus:
  UART IRQ → HAL_UARTEx_RxEventCallback (port) → MbSlave_RxFrame(buf, len)   [ISR: copy, flag]
  main/task → MbSlave_Handle()   [check, process, MbRtu_* callbacks]
            → MbPort_Send(buf, len)                [DE=1, transmit IT/DMA]
  UART IRQ → TxCplt (port) → MbSlave_TxDone()      [DE=0, restart reception]

LeBin:
  CDC_Receive_xS / UART RxEvent (port) → Lebin_RxBytes(data, len)   [ISR: ring buffer]
  main/task → Lebin_Handle()   [assemble packet, process]
            → LebinPort_Send(buf, len)   [STATUS_BUSY while previous transfer runs]
```

Application API:

```c
Status_t Lebin_Init(void);
Status_t Lebin_Handle(void);
Status_t MbSlave_Init(const MbSlave_Config_t *cfg);
Status_t MbSlave_Reconfigure(const MbSlave_Config_t *cfg);  /* applied when TX is idle */
Status_t MbSlave_Handle(void);
bool     MbSlave_IsTimeout(void);

typedef struct {
  uint8_t  address;      /* 1..247, saturated */
  uint32_t baudRate;     /* bit/s */
  uint8_t  parity;       /* MBSLAVE_PARITY_NONE/EVEN/ODD */
  uint8_t  stopBits;     /* 1 or 2 */
  uint32_t timeoutMs;    /* 0 = disabled */
} MbSlave_Config_t;
```

### 5.2 Port interface (`port.h`)

```c
uint32_t Port_GetTickMs(void);
void     Port_CriticalEnter(void);                   /* nestable, short sections only */
void     Port_CriticalExit(void);

/* Modbus */
Status_t MbPort_Init(const MbSlave_Config_t *cfg);   /* (re)configure UART, start reception */
Status_t MbPort_Send(const uint8_t *data, uint16_t len);
void     MbPort_NotifyFromIsr(void);                 /* RTOS task wake-up, may be empty */

/* LeBin */
Status_t LebinPort_Init(void);                       /* start reception (UART), nothing for USB */
bool     LebinPort_TxReady(void);                    /* previous response fully handed over */
Status_t LebinPort_Send(const uint8_t *data, uint16_t len);
void     LebinPort_NotifyFromIsr(void);

/* Upgrade */
Status_t UpgradePort_Begin(uint32_t size);           /* size 0: unknown, erase on the fly */
Status_t UpgradePort_Write(uint32_t offset, const uint8_t *data, uint32_t len);  /* data may be unaligned */
Status_t UpgradePort_Verify(uint32_t size);
void     UpgradePort_Apply(void);                    /* Modbus upgrade mode 2 */
```

Offsets are relative to the upgrade area; the port knows its flash layout and erase unit and
erases the units a write enters. The core uses no weak symbols: every hook is a plain function
defined by the port or `config_app.c`.

### 5.3 Concurrency

- ISR code only writes into core buffers. Modbus appends idle-line chunks to one frame buffer
  (length/tick read and reset inside `Port_CriticalEnter/Exit`); chunks received while the
  slave is transmitting are dropped (half-duplex RS-485).
- LeBin uses a single-producer/single-consumer ring buffer — no locks needed. A response is
  built only when `LebinPort_TxReady()`; one response per `Lebin_Handle()` call.
- UART reconfiguration is executed by `MbSlave_Handle()` only when no transmission is running.
- Under FreeRTOS `*_Handle()` runs in a task; the port may wake it via `*_NotifyFromIsr`.

### 5.4 Modbus framing

End of frame = UART idle line (`ReceiveToIdle`). If the received part fails the CRC check, the
core keeps it and waits for more bytes up to `max(t3.5, 2 ms)` (measured with
`Port_GetTickMs`), then drops it. Corrupted frames are dropped silently; broadcast (address 0)
is processed without a response.

Supported function codes: 3, 4, 6, 16. Others → exception 01. Address range outside
`MB_HOLD_LAST` / `MB_INPUT_LAST` (and outside the upgrade range when enabled) → exception 02.
Count 0, count > 125 (FC 3/4) or > 123 (FC 16), or FC 16 byte count ≠ 2 × count →
exception 03 (limits per Modbus spec; the old code used 123 for all).

### 5.5 Firmware upgrade

`fw_upgrade.c` exposes `Upgrade_Begin(size)`, `Upgrade_Write(offset, data, len)`,
`Upgrade_Finish()` and `Upgrade_Apply()`; it calls `UpgradePort_*`, rejects writes outside a
session and passes the received size to `UpgradePort_Verify`.

- LeBin packet 255 / ack 127 keeps its wire format: offset 0 → `Begin(0)`; data length 0 →
  `Finish`; length not multiple of 32 → retCode 2; retCode 0 OK, 1 error. The H7-specific
  `version < 34` check is removed.
- Modbus upgrade keeps its register layout (base 1000, 64-byte page, status/handshake); mode 1
  → `Begin(size)`, mode 0 → `Begin(0)`, mode 2 → `Apply`; its weak callbacks are replaced by
  calls into `fw_upgrade`.
- Enabled by `REGMAP_LIB_UPGRADE`.

### 5.6 Portability rules

- No unaligned access: packet fields are read/written via byte helpers in `lib_bytes.h`
  (`Bytes_GetU32Le`, `Bytes_PutU16Be`, …), safe on Cortex-M0/M0+.
- No `__REV16` in core (portable byte swap helper). Generated `mb_rtu_app.c` keeps `__REV16`
  (CMSIS, all Cortex-M).
- No `__attribute__((section))` in core except the firmware info blocks in `configuration.c`
  (through the overridable `CONF_SECTION(name)` macro).
- Loops over generated lists compare with `!=` so empty lists (`*_NUMBER` = 0) compile
  warning-free.
- No `Error_Handler()` in core: overflows drop data and increment an error counter.

## 6. `regmap export-lib`

```
regmap export-lib <dir> [--force] [--force-ports]
```

- Sources live in `src/regmap/firmware_lib/{core,port}/` as package data (stored with LF like
  other sources; written with CRLF + UTF-8 through `writer.encode`).
- Each core file carries a header line `@regmap-lib <version> sha256:<16 hex>`; the hash covers
  the file content without that line.
- Behaviour per file:

| File state in `<dir>` | Core file | Port file |
|---|---|---|
| missing | write | write |
| present, hash matches (unmodified) | update | keep |
| present, hash mismatch (edited) | error, exit 1 | keep |
| `--force` | overwrite | keep |
| `--force-ports` | — | overwrite |

- Missing `<dir>` is created. Errors are printed as `error: ...` (consistent with `cli.py`).
- Output lists written / updated / kept / unchanged files.

## 7. Testing

1. `tests/test_export_lib.py`: first export writes all files; re-export updates unmodified core
   and keeps ports; edited core → exit 1, `--force` overwrites; `--force-ports` overwrites ports;
   directory creation; header hash is valid for every packaged core file.
2. `tests/test_firmware_core.py`: compiles the core with host `gcc`
   (`-std=c99 -Wall -Wextra -fshort-enums`, `-Werror` for library sources) together with `regmap generate` output of
   `example/vms1511.yaml` and a fake port in `tests/c/` (captured TX, simulated tick, RAM flash),
   runs the C test binary. Skipped when `gcc` is not available; always runs in CI (Ubuntu).
   Cases:
   - Modbus: CRC known vectors, FC 3/4/6/16 incl. byte order, exceptions 01/02/03, broadcast,
     frame split across idle events, bad CRC, timeout flag.
   - LeBin: read/write registers, invalid ID, packet split into 64-byte USB chunks, several
     packets in one transfer, new-packet timeout, ring buffer overflow.
   - Upgrade: LeBin and Modbus paths through `Finish`/verify.
3. STM32 ports: manual build in STM32CubeIDE on one real project (H7), documented.

## 8. Bugs fixed relative to `firmware/`

| Where | Bug | Fix |
|---|---|---|
| `ComProto_WriteReg` | invalid ID: `0xF0` written into the request instead of the response | `0xF0` in the response, value size taken from the ID's length code |
| `ComProto_ProcessPacket` | unknown packet ID answered with a garbage response | no response |
| `ComProto_CheckProtoLength` | wrong start byte clears the whole buffer | resynchronise on the next start byte |
| `mb_upgrade.c` | register range is 84 bytes, storage 83 bytes → 1-byte overflow on the last register | reserved register in the storage |
| `MbUpgr_Handle` | `WriteDone` called even when the page write failed, masking the error | finish only after a successful write |
| `com_proto.c`, `configuration.c` | unaligned `*(uint32_t *)` access (HardFault on M0/M0+) | `memcpy` helpers |
| `usbd_cdc_if.c` | queue overflow calls `Error_Handler()` | drop + error counter |
| `MbSlave_CheckFrame` | FC 1, 2, 5 accepted as supported, then answered with exception only after processing; FC 6 missing | §5.4 |
| `MbSlave_ProcessFrame` | address bound check off by one (`addr + count > size + 1`) | exact range check |
| `ComProto_FillEvents` | reads a buffer nobody writes | removed (§3) |
| `modbus_slave.c` | `PRIO_IRQ_MODBUS` undefined in `common.h` | priorities in `regmap_lib_conf.h` |

## 9. Documentation

- `doc/firmware-lib.md`: contents of the export, `export-lib`, step-by-step integration
  (CubeMX UART/USB setup, `usbd_cdc_if.c` snippet, `*_Init` / `*_Handle`, bare-metal and
  FreeRTOS examples, linker symbols for upgrade).
- `doc/firmware-porting.md`: `port.h` contract, what to change for another family (flash sectors,
  USB stack).
- Update `doc/cli.md`, `doc/index.md`; one line in `README.md`.

## 10. Open items

- Whether to delete `firmware/` after migration or keep it outside git (decided at the end).
