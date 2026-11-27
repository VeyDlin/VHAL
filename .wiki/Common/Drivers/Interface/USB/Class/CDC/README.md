# CDC Class: Serial Port and Network Adapter

The Communications Device Class (CDC) has two functions in this stack. Each one has a page of its own:

- CDC-ACM, the serial port: the host sees a COM port. It is described on this page.
- CDC-NCM, the network adapter: the host sees an Ethernet link, and the device moves Ethernet frames. It is on the [CDC-NCM Network Adapter](NcmFunction.md) page.

The serial port (CDC-ACM) is the Communications Device Class in its abstract control model (CDC 1.2, PSTN 1.2). The device has a communication interface for the line settings and the control lines, a notification endpoint for the UART state, and a data interface with a bulk IN and a bulk OUT endpoint carrying the bytes. The host sees a COM port. The host simulator tests the stack, and the class check passed the serial port on a board (see the [usbclasscheck page](../../.tool/usbclasscheck/README.md)). The [Measured Results](../../README.md#measured-results) section has no CDC numbers. Host support is on the [Host Support](../../README.md#host-support) section.

Headers of the serial port:

- `#include <Drivers/Interface/USB/Class/CDC/USBCdcAcmBuilder.h>`: the interface, for the configuration builder
- `#include <Drivers/Interface/USB/Class/CDC/CdcAcmFunction.h>`: the function, the class requests and the data rings
- `#include <Drivers/Interface/USB/Class/CDC/USBCdcProtocol.h>`: the request codes, the line coding and the UART state bits
- `#include <Utilities/Bootloader/Communication/USBCommunication.h>`: the bootloader's transport over this function (see [Bootloader](../../../../../Utilities/Bootloader/README.md#usbcommunication----usb-transport))

## Host Drivers

No driver is shipped with the device, and no INF file is needed. The class and subclass identify the function to the host's in-box driver:

| Host | Driver | Notes |
|------|--------|-------|
| Windows 10 and later | `usbser.sys`, bound from the interface association (class 0x02, subclass 0x02) | The port appears as a COM port. Windows binds it with no INF, as it does for any CDC device |
| Linux | `cdc_acm` | The port appears as `/dev/ttyACMn`, bound from the class codes |
| macOS | The in-box CDC driver (`AppleUSBCDC`) | Untested on hardware. The port appears under `/dev/cu.usbmodem*` |
| Android | The host's USB serial support, where it exists | Untested on hardware |

The CDC tests check the bytes the hosts read and the requests they send. They do not run the host drivers.

## Class Builder

`USBCdcAcmBuilder` plugs into `USBConfigurationBuilder::Function()` (see [Descriptor](../../Descriptor/README.md#class-builders)). The configuration writes an interface association over both interfaces, then:

| Interface | Descriptors, in order |
|-----------|-----------------------|
| 0, communication (class 0x02, subclass 0x02, protocol 0x00) | Header (bcdCDC 0x0110), Call Management (bmCapabilities 0, data interface 1), Abstract Control Management (bmCapabilities 0x02), Union (control 0, subordinate 1), then the notification endpoint |
| 1, data (class 0x0A, subclass 0, protocol 0) | Bulk IN and bulk OUT, 64 bytes each, with no alternate setting |

The endpoint order is the one `CdcAcmFunction` reads, and the one `Function(i).endpoints` gives: notification IN (interrupt), bulk IN, bulk OUT.

| Method | Description |
|--------|-------------|
| `Name(text)` | The communication interface's string. Optional |
| `NotificationIn(address)` | The notification endpoint, an IN address. Without it the configuration builder assigns the number |
| `BulkIn(address)` | The bulk IN endpoint. Without it the configuration builder assigns the number |
| `BulkOut(address)` | The bulk OUT endpoint. Without it the configuration builder assigns the number |
| `IntervalMs(n)` | Polling interval of the notification endpoint, 1 to 255 ms. Default 16 |

The notification endpoint is 16 bytes, which holds the 10-byte SERIAL_STATE notification with room to spare. The builder assigns the numbers in the order of the endpoints (the notification first), each on a number no endpoint of either direction uses yet, so the three addresses are on three numbers.

Rules. A violated rule is a compile error with its message:

- `NotificationIn needs an IN address (bit 7 set)`, `BulkIn needs an IN address (bit 7 set)`, `BulkOut needs an OUT address (bit 7 clear)`: the direction of an explicit address.
- `endpoint 0 belongs to the control pipe`: endpoint number 0 is not an endpoint of the function.
- `IntervalMs is 1 to 255 ms`: an interval of 0.
- `duplicate endpoint address`: two endpoints given the same address.

## Function

`CdcAcmFunction<TxSize, RxSize, PacketsPerTransfer = 8>` is a `USBFunction`. Its data interface is the bulk engine (`Class/Common/BulkEndpointEngine.h`) in buffered mode, so the data path is the same as the vendor bulk function's: the same two IN stages, the same claim and the same halt and reset rules.

The bulk OUT transfer is one packet (64 bytes), and `PacketsPerTransfer` sizes the bulk IN transfers only. An OUT transfer completes on a short packet or when its buffer is full, so a buffer of several packets held a host write that was a whole number of packets, with no zero-length packet after it, and such a write never reached `Read()`. Windows' usbser.sys sends no zero-length packet by default, so the one-packet transfer is what delivers every write whole. The write of 64 bytes that hung on the vendor raw channel on the first hardware run has the same cause. `TestCdcAcmData.cpp` checks a 64-byte and a 128-byte write with no zero-length packet.

The bulk IN side ends its runs as the vendor bulk function does (see the [Vendor page](../Vendor/README.md#packets-and-transfers), its rule on how an IN run ends). A whole-packet run ends with a zero-length packet only when nothing is queued behind it, for a stage of any size. A write of exactly one stage (`PacketsPerTransfer` * 64 bytes) with nothing behind it ends with a zero-length packet, so a host read larger than the stage returns. Two stages written at once have no zero-length packet between them, and one after the last. A write of 64 bytes keeps its zero-length packet, which is decided when its stage starts.

```cpp
using Usb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("VHAL")
		.Product("Serial")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBCdcAcmBuilder().Name("Serial")));
}>;

CdcAcmFunction<512, 256, 4> serial(Usb::Function(0));
```

Data:

| Method | Description |
|--------|-------------|
| `Write(data)` | Queues bytes for the host. Returns how many fit in the TX ring. 0 while not configured |
| `Read(data)` | Copies received bytes out. 0 while not configured |
| `GetReadableCount()`, `GetWritableCount()` | Bytes waiting to be read, and room for writes |
| `Process()` | Call it from the loop. It starts the transfers and sends the pending notification |
| `IsConfigured()`, `GetSessionGeneration()` | The session state. The bootloader transport reads them |

Control lines and line coding:

| Method | Description |
|--------|-------------|
| `IsTerminalOpen()` | DTR, the "terminal open" signal the host sets. Cleared on deconfiguration and on a bus reset |
| `GetLineCoding()` | The line coding the host last set, or 115200 8N1 before it sets one. A bus reset returns it to the default |
| `SetSerialState(bits)` | Sets the UART state bitmap (bit 0 RX carrier, bit 1 TX carrier, bit 2 break, bit 3 ring, bit 4 framing, bit 5 parity, bit 6 overrun). `Process()` sends it |

The handlers the application sets. Each is a `Callback::Delegate`, so it never allocates, and each runs in interrupt context when the host's request arrives, so it must be short. Set them before the host configures the device. Once the host has configured it, they return `busy`.

| Setter | Delegate | Called for |
|--------|----------|------------|
| `SetLineCodingHandler()` | `void(const USBCdcLineCoding&)` | SET_LINE_CODING, after the coding is stored |
| `SetControlLineStateHandler()` | `void(bool dtr, bool rts)` | SET_CONTROL_LINE_STATE, only when a line changes |
| `SetBreakHandler()` | `void(uint16 durationMs)` | SEND_BREAK: milliseconds; 0 ends a break; 0xFFFF lasts until the next SEND_BREAK(0) |

`USBCdcLineCoding` holds `dteRate`, `stopBits` (0 one, 1 one and a half, 2 two), `parity` (0 none, 1 odd, 2 even, 3 mark, 4 space) and `dataBits` (5, 6, 7, 8 or 16).

## Requests

The requests go to the communication interface (`wIndex` is its number). Anything else is stalled: a request to the data interface, a wrong direction or length, an unknown class request, and a line coding the port cannot run.

| Request | bmRequestType | bRequest | Data stage | Effect |
|---------|---------------|----------|------------|--------|
| SET_LINE_CODING | 0x21 | 0x20 | 7 bytes from the host | Stores the coding, then calls the line coding handler. A rate of 0, stop-bit code above 2, parity code above 4, or data bits other than 5, 6, 7, 8 or 16 is stalled, and the stored coding stays |
| GET_LINE_CODING | 0xA1 | 0x21 | 7 bytes to the host | Answers with the stored coding |
| SET_CONTROL_LINE_STATE | 0x21 | 0x22 | none | DTR is bit 0 and RTS is bit 1 of `wValue`. Updates `IsTerminalOpen()` |
| SEND_BREAK | 0x21 | 0x23 | none | Calls the break handler with `wValue` |

A line coding block is 7 bytes: `dwDTERate` little-endian, then `bCharFormat`, `bParityType` and `bDataBits`. The default is `00 C2 01 00 00 00 08`.

## Notifications

`SetSerialState()` only records the state. `Process()` sends it as a SERIAL_STATE notification on the notification endpoint (PSTN 1.2 section 6.3.5), 10 bytes:

```
A1 20 00 00 00 00 02 00 <state low> <state high>
```

The header is bmRequestType 0xA1, bNotification 0x20, wValue 0, wIndex the communication interface, wLength 2. The example bytes have wIndex 0 (interface 0); another interface number changes bytes 4 and 5. Only one notification is in flight at a time. A change made while one is in flight is not written into its buffer; the newest state is sent when the one in flight completes, from the interrupt. The state is sent once after each configuration, so the host learns it, and again when a halt of the notification endpoint clears, because the notification in flight is lost with the halt.

## Interrupt Context and Critical Sections

| Code | Context | Rule |
|------|---------|------|
| `OnConfigured`, `OnDeconfigured`, `OnBusReset`, `OnControlRequest`, `OnControlDataReceived`, `OnTransferComplete`, `OnEndpointHaltChanged` | Interrupt | Short. No section is entered, because `OnDeconfigured` also runs inside `Stop()`, which the caller wraps in a section |
| Line coding, control line and break handlers | Interrupt | Short. The coding is stored before its handler runs |
| `Write()`, `Read()`, `SetSerialState()`, `Process()` | Thread | The rings and the notification state are shared with the interrupt through short sections, as in the vendor bulk function |
| The handler setters | Thread | Before the host configures the device |

## Tests

The host tests are in `.test` and run with `./test.cmd USB`:

- `TestCdcAcmDescriptors.cpp`: the device descriptor and the configuration with automatic addresses, explicit addresses and a 4 ms interval, checked against arrays written by hand from the USB 2.0 layout, the interface association, CDC 1.2 section 5.2 and PSTN 1.2.
- `TestCdcAcmRequests.cpp`: each class request with its bytes, the handlers and their change-only rule, the stalls (the data interface, wrong length, wrong direction, unknown request, a request to the device, a coding out of range) with the edges that are accepted, a bus reset restoring DTR, RTS and the default coding, and the busy handler setters.
- `TestCdcAcmData.cpp`: data both ways through the rings, 1000 bytes in order, the bus reset, the halt and clear of each bulk endpoint (one stage, and two stages with the first one committed), deconfiguration dropping DTR, the notification halt leaving the bulk pair alone, and the zero-length packet of the IN run (one full stage, two stages at once, and a 64-byte write), seen through the DATA PID.
- `TestCdcAcmNotification.cpp`: the SERIAL_STATE bytes, the initial state, a change in flight sending the newest state, the halt and clear, and the resend after reconfiguration.
- `TestUsbCommunication.cpp`: the bootloader transport over the CDC function (DTR, all-or-nothing `SendData`, both directions, the empty span on a disconnect and on a session change, a bus reset that drops DTR, a reset from inside the data callback with the empty span before the new session's bytes) and over the vendor function.
- `BuilderErrors/cdc_*.cpp`: the compile errors above, each with the message it must give.

The network adapter's tests (`TestNcm*.cpp` and `BuilderErrors/ncm_*.cpp`) are on the [CDC-NCM Network Adapter](NcmFunction.md#tests) page.
