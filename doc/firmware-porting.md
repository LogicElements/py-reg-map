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
