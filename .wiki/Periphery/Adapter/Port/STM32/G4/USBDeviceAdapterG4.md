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
  - Bulk: 8, 16, 32 or 64 bytes.
  - Interrupt: 1 to 64 bytes.
  - Isochronous: 1 to 1023 bytes, see [Isochronous Endpoints](#isochronous-endpoints).
- Bulk endpoints can be double-buffered on request (`EP_KIND` set). An isochronous endpoint always has its two buffers in the port, without `EP_KIND` (see [Isochronous Endpoints](#isochronous-endpoints)).
- The two directions of one endpoint number share one endpoint register (EP`n`R). A double-buffered endpoint uses both directions of its number:
  - Both directions must have the same type, or `OpenEndpoint()` returns `invalidParameter`.
  - A double-buffered request while the other direction is open returns `notSupported`. The endpoints stay as they were.
  - A single-buffered request while the other direction is double-buffered returns `invalidParameter`.
  - So IN `0x81` and OUT `0x02` can both be double-buffered. IN `0x81` and OUT `0x01` cannot: the vendor function opens them single-buffered.
- `CloseEndpoint()` and `CloseAllEndpoints()` free the blocks. A completion the interrupt still holds for a closed direction (its `CTR` flag stays set through the close) only clears that flag. `OpenEndpoint()` clears the flag of the direction it opens and no other, so a completion of the old endpoint cannot be served against the new one.

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

## Isochronous Endpoints

The fsdev port carries isochronous endpoints as the [adapter contract](../../../USBDeviceAdapter.md#isochronous-endpoints) describes them. This section is the register side. The model it follows is settled from three sources, and the hardware run at the end of this section confirms its streaming path.

- ST's G4 driver in this repository (`Periphery/Adapter/Port/STM32/G4/Drivers/HAL/Src/stm32g4xx_ll_usb.c`, about lines 305-313 and 535-557, and `stm32g4xx_hal_pcd.c`, about lines 1785-1810 and 1842-1856). It clears `EP_KIND` for an isochronous endpoint, writes an isochronous IN packet into the buffer `DTOG_TX` selects and never writes `SW_BUF` for it, and zeroes the `COUNT` of the buffer it has just sent.
- ChromeOS EC's `chip/stm32/usb_isochronous.c`, a production fsdev isochronous IN driver. It sets `STAT_TX` to VALID once and never changes it, never writes `SW_BUF`, and zeroes the count of each buffer after it is sent, "so we do not transmit the data twice".
- The reference manual of the same block (RM0440, USB chapter; the same text is in RM0091 and RM0367). The buffer is the one the `DTOG` bit of the direction selects. Hardware toggles `DTOG` at the end of each transaction, "making buffer swapping completely software independent", and a transaction completion does not change `STAT`.

The model:

- `EP_TYPE` is `ISO` and `EP_KIND` is clear. The endpoint has the two buffer descriptors of its direction, as a double-buffered bulk endpoint does, and the port gives it both blocks.
- `STAT` is `VALID` from open and stays so. The port writes no `NAK` and no `STALL` to an isochronous endpoint, and it never writes `SW_BUF` (`DTOG_RX` for IN, `DTOG_TX` for OUT).
- Every valid token is served, armed or not. On each IN token the controller sends the buffer `DTOG_TX` selects with that buffer's `COUNT` (0 is a zero-length packet), then toggles `DTOG_TX` and sets `CTR_TX`. On each OUT token it stores the packet in the buffer `DTOG_RX` selects, toggles `DTOG_RX` and sets `CTR_RX`. Neither depends on `SW_BUF` or `EP_KIND`.
- No data PID is assumed for an isochronous packet. USB 2.0 full speed uses DATA0 for them, but this peripheral's PID is not established, so the port and the host tests assert none.

The port's side:

- IN arm from the endpoint's own completion callback: `StartInTransfer()` copies the packet into the buffer `DTOG_TX` selects and sets its `COUNT`. It writes no register. The completion of the packet before has already moved `DTOG_TX` onto the buffer that was not sent, so the copy fills the next packet's buffer, and the packet goes out at the next token.
- IN arm from anywhere else (thread context, the control request that selects the alternate setting, another endpoint's callback): the packet is deferred. No copy runs, `busy` is returned until it completes, and the next `CTR_TX` of the endpoint writes it into the buffer `DTOG_TX` then selects, which is free. That token sent a zero-length packet and completes nothing, and the packet goes out at the token after it. The port's completion callback is marked for the arm of the first case by a flag set around it (`txCompleting`), so the two cases are told apart.
- IN completion (`CTR_TX`): the port reads `DTOG_TX`, zeroes the `COUNT` of the buffer it no longer selects (the one just sent), clears `CTR_TX` without touching the toggles, and completes the transfer if one was armed. With nothing armed, the zero-length packet of that frame gets the first two steps and no event. With a deferred packet pending, the completion writes it as described above and completes nothing.
- OUT completion (`CTR_RX`): the port reads the buffer `DTOG_RX` no longer selects (the one just filled), clears `CTR_RX`, and copies the packet out when a transfer is armed. A packet that arrives with nothing armed is dropped. A packet longer than its block, or than the armed buffer, is dropped whole with `DataOverrun`. Each OUT completion also toggles `DTOG_TX` once, in the same write that clears `CTR_RX`, as ST's `PCD_FREE_USER_BUFFER` does (`stm32g4xx_hal_pcd.c`, about lines 1785-1800). The model says the controller needs no such toggle for OUT. The toggle is kept in case the model is wrong: it moves no IN buffer, because a number carries one isochronous direction.
- `Disconnect()`, `CloseEndpoint()` and a bus reset drop a deferred or armed packet with no event, and zero both `COUNT`s of an open isochronous IN endpoint, so nothing armed before the change goes out after the next connection.
- OUT arm: `StartOutTransfer()` writes no register. It records the buffer.
- `ClearStall()` returns `ok` and changes nothing. `Stall()` returns `notExist` for a direction that is not open, and `invalidParameter` for an open one. `IsStalled()` is false.
- One register serves both directions of a number, so an isochronous IN and an isochronous OUT cannot both be open on one number (`notSupported`).
- The `CRC` of a packet is not reported. A bus error raises `ISTR.ERR`, which the port reports as `Error::Unknown`.

What follows from the model:

- A frame with nothing armed is a zero-length IN packet. The earlier packet is never sent again, because its `COUNT` was zeroed when it went out.
- Arming at the start of a frame races that frame's token, because hosts schedule periodic transfers early in the frame. The port arms from the completion of the packet before, so the packet is in place about a frame before its token. ST arms the same way.
- A packet armed from outside the completion callback waits for the next token, as described above. Its copy then runs in the interrupt that the token raises, so no copy can land while the host reads the buffer. The first packet of a stream armed from thread context therefore goes out at the second token, not the first.
- The bound: the interrupt must be served within one frame of the token. A later service merges the completions of two tokens, and the port cannot tell which buffer went out.

A packet armed while the direction's `CTR_TX` is already set, and not yet served by the interrupt, is written by that interrupt after the finished token. It therefore goes out at the next token, one earlier than a packet armed after the interrupt. The write still follows a finished token, so it never races the host. A register-level test covers the deferral (`TestFsdevPort.cpp`).

Stale flags: `OpenEndpoint()` clears the `CTR` flag of the direction it opens. A completion left set by a token before a close is then not served against the transfer armed after the reopen. The other direction keeps its flag, since its completion may not be served yet. `CloseEndpoint()` writes `STAT` `DISABLED` before it zeroes the `COUNT`s of an isochronous IN, so the endpoint answers no token while its buffers are emptied.

### Measured on hardware

On the STM32G431 under Windows 11, with the in-box USBAudio class, both directions stream at one packet per frame. The microphone (isochronous IN) delivered 1000 packets per second with one underrun at stream start, and the speaker (isochronous OUT) received 1000 packets per second with no error.

Not measured yet: the zero-length packet that the first token sends after a deferred arm (the first microphone packet is armed by `SET_INTERFACE`, so it is the deferred case), and the one-frame bound on the interrupt. The host tests cover the model on the register simulator (`TestFsdevPort.cpp`) and at the contract (`TestIsochronous.cpp`). The simulator adapter models the deferral as the contract states it (see the contract page).

### Packet Memory Cost

An isochronous endpoint takes two packet memory blocks of its direction, one per buffer. A block is the packet size rounded up to an even number of bytes, and to a multiple of 32 bytes above 62 bytes (the `COUNT_RX` block count can express only that):

| Packet size | Block | Endpoint cost |
|-------------|-------|---------------|
| 64 | 64 | 128 |
| 128 | 128 | 256 |
| 192 | 192 | 384 |
| 200 | 224 | 448 |
| 300 | 320 | 640 |
| 416 | 416 | 832 (the largest that fits beside endpoint 0) |
| 417 | 448 | 896 (does not fit: `outOfMemory`) |
| 1023 | 1024 | 2048 (does not fit: `outOfMemory`) |

The 1 KB of packet memory holds the buffer table (64 bytes: eight entries of eight bytes, `BufferTableSize`) and endpoint 0 (128 bytes: 64 transmit and 64 receive), which leaves 832 bytes. Two blocks of 416 bytes take exactly those 832, so 416 is the largest isochronous packet the port can place beside endpoint 0. The first block lands at 192 and the second at 608, ending at 1024. A 417-byte packet rounds to a 448 block, and two of those need 896, which does not fit. One 300-byte isochronous endpoint (640 bytes) fits, and a second one does not. Two 128-byte ones (256 bytes each) fit. Every block starts on a 4-byte boundary, so a block that is not a multiple of four leaves a gap: six 62-byte isochronous endpoints end at 958, and a 44-byte one beside them does not fit, although the sizes add up to 832. The simulator places blocks in the same way. The cost comes out of the same pool as the bulk and interrupt endpoints, so every isochronous endpoint reduces what the others can get.

The table is 64 bytes whatever endpoints are open, because it has an entry for each of the eight endpoint numbers. The 32-byte rounding is applied to IN blocks too, although only an OUT buffer's `COUNT_RX` needs it, so an IN block is up to 28 bytes larger than its packet. Example layouts of the audio class, in bytes of packet memory (the Audio README has the full table):

| Layout | Blocks | Total | Fits 1024 |
|--------|--------|-------|-----------|
| Mono 48 kHz microphone (98) and stereo 48 kHz speaker (196) | 128 and 224, two each | 896 | Yes |
| Stereo 48 kHz microphone (196) and mono 48 kHz speaker (98) | 224 and 128, two each | 896 | Yes |
| Stereo 16 kHz microphone (68) and stereo 16 kHz speaker (68) | 96 and 96, two each | 576 | Yes |
| Stereo 48 kHz microphone and stereo 48 kHz speaker | 224 and 224, two each | 1088 | No: the second open is `outOfMemory` |

Two stereo 48 kHz isochronous endpoints do not fit, whichever opens first. The second is refused and the first keeps its blocks. Closing the IN frees its 448 bytes, and the OUT then fits at 192 and 416 (`TestIsoStereoPairOutOfMemory` in `TestFsdevPort.cpp`).

An isochronous endpoint and the other direction of its number share one register. The other direction must also be isochronous, or `OpenEndpoint()` returns `invalidParameter`. An isochronous OUT beside an open IN of the same number is `notSupported`, because the register cannot hold the second pair of buffers next to it. So an isochronous IN and OUT must use different numbers, such as `0x81` and `0x02`.

## Not Supported on the G4 Port

- High speed. The G4 fsdev is full speed only.
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
| Isochronous packet size | 1 to 1023 accepted. 416 is the largest that fits beside endpoint 0; a larger one is `outOfMemory` |
| Speed | Full |
