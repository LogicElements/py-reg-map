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
   With DMA on STM32H7 the port buffers must lie in RAM the DMA can reach (not DTCM): define
   `MB_PORT_DMA_BUFFER_ATTR` as a section attribute placing them in AXI/SRAM; the port cleans
   and invalidates the D-cache around transfers itself.

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
   and `.sectionEndOfFlash`. A project without one of these linker symbols defines its C name
   as a macro instead (e.g. `#define CONF_C_APP_BUFFER_OFFSET ((const uint8_t *)0x08100000)`
   in `main.h`); `configuration.h` then skips the declaration.

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
