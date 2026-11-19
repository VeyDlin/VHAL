# USBDeviceAdapterG4

The [USB device adapter](../../../USBDeviceAdapter.md) on the STM32G431, G441 and G471 full-speed USB peripheral, `fsdev`. The register code is shared with the other `fsdev` families and lives in `Port/STM32/Common/USBFsdev.h`. The G4 file only gives the family constants.

Header: `#include <Adapter/Port/STM32/G4/USBDeviceAdapterG4.h>`

Class: `USBDeviceAdapterG4`. Alias: `AUSB`. The constructor takes the peripheral handle, which defaults to `USB`.

Enabled by `#define VHAL_STM32_G4_USB`, which `Periphery/Adapter/Port/STM32/Adapter.h` checks before it includes the port. The port uses the G4 CMSIS device definitions (`USB`, `RCC`, `USB_PMAADDR`), which must be included before this header, as `Adapter.h` does.

The adapter does not set up the clock, the pins or the interrupt. The BSP does, before the first `SetParameters()`.

## Clock and Interrupt (BSP)

The BSP gives the peripheral a 48 MHz clock from HSI48. CRS trims HSI48 against the USB start-of-frame, so no crystal is needed. The BSP then enables the USB bus clock and sets the interrupt. The code below is the bench firmware's `BSP::InitUsbClock()` (`.demo/STM32G4/BSP/BSP.cpp`), which is the reference for this port. The [USB stack page](../../../../../Common/Drivers/Interface/USB/README.md#1-clock-and-interrupt-in-the-bsp) shows the same code in its build steps.

```cpp
void BSP::InitUsbClock() {
	// HSI48 is the 48 MHz USB clock. CRS trims it against the USB start-of-frame (1 kHz), so the clock needs no crystal and
	// stays within the USB tolerance. The reload value is 48 MHz / 1 kHz - 1.
	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_CRS);

	LL_RCC_HSI48_Enable();
	while (LL_RCC_HSI48_IsReady() != 1) {}

	LL_CRS_ConfigSynchronization(
		LL_CRS_HSI48CALIBRATION_DEFAULT,
		LL_CRS_ERRORLIMIT_DEFAULT,
		47999U,
		LL_CRS_SYNC_SOURCE_USB | LL_CRS_SYNC_POLARITY_RISING | LL_CRS_SYNC_DIV_1
	);
	LL_CRS_EnableFreqErrorCounter();
	LL_CRS_EnableAutoTrimming();

	LL_RCC_SetUSBClockSource(LL_RCC_USB_CLKSOURCE_HSI48);
	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USB);

	// Only USB_LP serves the controller. USB_HP stays disabled in the NVIC: the adapter's flags would be served from two
	// vectors otherwise.
	NVIC_SetPriority(USB_LP_IRQn, 1);
	NVIC_EnableIRQ(USB_LP_IRQn);
}
```

Notes:

- The reload value 47999 (`0xBB7F`) is 48 MHz / 1 kHz - 1, which is the 1 ms USB start-of-frame at 48 MHz. `LL_CRS_RELOADVALUE_DEFAULT` is the same value.
- `Initialization()` checks `RCC->APB1ENR1` for the USB clock. Without it, `SetParameters()` returns `notReady`. The check runs before the adapter's `beforePeripheryInit` hook, so the hook cannot enable the clock in time. Enable it in the BSP before `SetParameters()`.
- The vector function forwards to the adapter: `USB_LP_IRQHandler` calls `BSP::usbAdapter.IrqHandler()` (`BSP/IRQ/DeviceIrq.cpp` of the bench firmware).
- The pins PA11 (USB D-) and PA12 (USB D+) must be set to the USB alternate function. The number is in the device datasheet's alternate function table. The repository's G4 headers do not define it, so take it from the datasheet. The bench firmware uses 10 for both (`BSP::InitIO()`).

## Family Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `PmaBase` | `USB_PMAADDR` | Packet memory |
| `PmaSize` | 1024 | Bytes of packet memory |
| `PmaElementStride` | 2 | Bytes between 16-bit packet memory elements |
| `HardwareEndpointCount` | 8 | Endpoint registers |
| `IsClockEnabled()` | | The USB bus clock is on |

## Initialization

`SetParameters()` stores the parameters and calls `Initialization()`:

1. Check the USB clock. `notReady` if off.
2. Check `endpointCount`. `invalidParameter` if 0 or above 8.
3. Call the `beforePeripheryInit` hook, if set.
4. Reset the peripheral: `FRes` for about 10 µs, then clear the control and interrupt registers.
5. Set the buffer table, clear every endpoint register.
6. Give endpoint 0 a 64-byte transmit buffer and a 64-byte receive buffer in packet memory.
7. Enable the interrupts: correct transfer, reset, suspend, wakeup, error and packet memory overrun. Start-of-frame too when `startOfFrameEvents` is set.
8. Enable the function (`EF`). The pull-up stays off.
9. Call the `afterPeripheryInit` hook, if set.

The NVIC is not touched. The BSP enables the line.

## Connect and Disconnect

- `Connect()` sets the internal pull-up, `DPPU` in `USB_BCDR`. The host sees the device at once.
- `Disconnect()` clears it. Transfers in flight are dropped without events. Endpoints stay open, so the core can keep its configuration across a detach.
- Both return `notReady` before `SetParameters()` succeeded.

## Endpoints and Packet Memory

- Endpoint 0 uses 128 bytes of packet memory: 64 transmit and 64 receive. Opened at initialization.
- Other endpoints are placed in the first free block that fits (first-fit). `OpenEndpoint()` returns `outOfMemory` when none fits.
- A double-buffered bulk endpoint uses two blocks of its packet size, one per hardware buffer.
- `OpenEndpoint()` checks `endpointCount`, the type and the packet size:
  - Control type is refused.
  - Isochronous is refused.
  - Bulk: 8, 16, 32 or 64 bytes.
  - Interrupt: 1 to 64 bytes.
- Only bulk endpoints can be double-buffered.
- The two directions of one endpoint number share one endpoint register (EP`n`R). A double-buffered endpoint uses both directions of its number:
  - Both directions must have the same type, or `OpenEndpoint()` returns `invalidParameter`.
  - A double-buffered request while the other direction is open returns `notSupported`. The endpoints stay as they were.
  - A single-buffered request while the other direction is double-buffered returns `invalidParameter`.
  - So IN `0x81` and OUT `0x02` can both be double-buffered. IN `0x81` and OUT `0x01` cannot: the vendor function opens them single-buffered.
- `CloseEndpoint()` and `CloseAllEndpoints()` free the blocks.

## SET_ADDRESS

`SetAddress()` stores the address as pending. The port writes it into the device address register at the end of the endpoint 0 IN transfer that is the status stage of SET_ADDRESS. That is before the transfer's completion is reported. A SETUP before the status discards the pending address.

## Interrupts and Errors

`IrqHandler()` serves the whole controller. The application calls it only from `USB_LP_IRQHandler`. It handles:

| Flag | Action |
|------|--------|
| Reset | Closes all endpoints except 0, opens endpoint 0 again, drops a pending address and reports `onBusReset` |
| Suspend | Enters low-power mode and reports `onSuspend` |
| Wakeup | Leaves low-power mode and reports `onResume`. This is the host's resume |
| Error | Clears the flag. Reports `Error::Unknown` |
| Packet memory overrun | Clears the flag. Reports `Error::Unknown` |
| Start of frame | Clears the flag. Reports `onStartOfFrame` when `startOfFrameEvents` is set |
| Expected start of frame | Ends the resume signalling of `RemoteWakeup()` after its last frame, and reports `onResume` |
| Correct transfer | Serves each endpoint in turn, until none is left |

- `DataOverrun` is reported when an OUT packet does not fit the room left in its buffer.
- The port does not report `Crc`, `BitStuffing`, `Timeout` or `Framing`. The core's error count only includes errors reported by the flags above.
- `USB_HP_IRQn` must stay disabled. Otherwise the same flags are served from two vectors.

## Not Supported on the G4 Port

- High speed. The G4 fsdev is full speed only.
- Isochronous endpoints.
- VBUS sensing. The G4 fsdev has no VBUS detector, so the adapter has no option for it. A GPIO detector can be added later through the BSP.
- Link power management and battery charging detection.

## Limits

| Item | Value |
|------|-------|
| Endpoint numbers | 1 to 7 can be opened (`endpointCount` up to 8, endpoint 0 included) |
| Default `endpointCount` | 4 |
| Packet memory | 1024 bytes, shared |
| Endpoint 0 packet memory | 128 bytes |
| Bulk packet size | 8, 16, 32, 64 |
| Interrupt packet size | 1 to 64 |
| Speed | Full |
