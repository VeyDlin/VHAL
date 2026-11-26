# CDC-NCM Network Adapter

CDC-NCM (USB CDC Subclass Specification for Network Control Model Devices 1.0) makes the device an Ethernet adapter. The host sees a network interface, and each side moves whole Ethernet frames inside network transfer blocks (NTBs). The stack implements NTB-16 only. The stack is tested in the host simulator only; no hardware measurement is recorded for NCM yet.

The function carries Ethernet frames only. VHAL has no TCP/IP stack for STM32 yet, so [SocketAdapter](../../../../../../Periphery/Adapter/SocketAdapter.md) and [HttpClient](../../../Network/Http/README.md) do not run over it. The USBNetwork demo answers DHCP, ARP and ping with its own small responder, in the demo's `Network` folder (`.demo/STM32G4/Application/DemoUSBNetwork/Network`).

The serial port, CDC-ACM, is on the [CDC Class page](README.md).

Headers:

- `#include <Drivers/Interface/USB/Class/CDC/USBCdcNcmBuilder.h>`: the interfaces, for the configuration builder
- `#include <Drivers/Interface/USB/Class/CDC/NcmFunction.h>`: the function, the NCM requests and the frame queues
- `#include <Drivers/Interface/USB/Class/CDC/NcmNtb16.h>`: the NTB-16 codec, a parser and a builder with no device dependency. The function uses it; so can a test or a tool

## Class Builder

`USBCdcNcmBuilder` plugs into `USBConfigurationBuilder::Function()`. The configuration writes an interface association over both interfaces, then:

| Interface | Descriptors, in order |
|-----------|-----------------------|
| 0, communication (class 0x02, subclass 0x0D, protocol 0x00) | Header (bcdCDC 0x0110), Union (control 0, subordinate 1), Ethernet Networking (iMACAddress is a run-time string), NCM (bcdNcmVersion 1.00, bmNetworkCapabilities 0x21), then the notification endpoint |
| 1, data (class 0x0A, protocol 0x01, NTB) | Alternate 0 with no endpoints (no traffic), then alternate 1 with the bulk IN and bulk OUT endpoints, 64 bytes each |

The endpoint order is the one `NcmFunction` reads, and the one `Function(i).endpoints` gives: notification IN (interrupt, 16 bytes), bulk IN, bulk OUT.

| Method | Description |
|--------|-------------|
| `Name(text)` | The communication interface's string. Optional |
| `NotificationIn(address)`, `BulkIn(address)`, `BulkOut(address)` | The endpoint addresses. Without them the configuration builder assigns the numbers |
| `IntervalMs(n)` | Polling interval of the notification endpoint, 1 to 255 ms. Default 16 |

The rules are the ones of `USBCdcAcmBuilder`, with the same messages (`BulkIn needs an IN address (bit 7 set)`, `endpoint 0 belongs to the control pipe`, `IntervalMs is 1 to 255 ms`), checked by the snippets `BuilderErrors/ncm_*.cpp`.

The builder reserves one run-time string, the MAC address. The capabilities byte advertises only what the function implements: SET_ETHERNET_PACKET_FILTER (bit 0) and the 8-byte NTB input size (bit 5).

## MAC Address

`iMACAddress` names the 48-bit MAC address the host uses for its own network interface, as 12 upper-case hex digits. The device's own IP stack must use a different address. The application gives the host's address before the device starts:

```cpp
using Usb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0005) // The network demo's test PID (DemoProducts.h). A product uses its own.
		.DeviceRelease(0x0100)
		.Manufacturer("VHAL")
		.Product("Ethernet")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBCdcNcmBuilder().Name("Ethernet")));
}>;

NcmFunction<2048, 2048, 4, 4> ethernet(Usb::Function(0));
USBFunction* functions[] = { &ethernet };
USBDevice device(BSP::usbAdapter, Usb::Set, functions);

// Static storage: the device keeps the span until it stops.
constexpr std::array<uint8, 6> HostMac = {0x02, 0x00, 0x00, 0xAB, 0xCD, 0xEF};
std::array<uint8, 26> macDescriptor = {};
if (MakeUSBHexStringDescriptor(HostMac, macDescriptor) != ResultStatus::ok) {
	System::CriticalError("MAC string failed");
}

device.SetRuntimeString(ethernet.MacStringIndex(), macDescriptor);
device.Start(USBDeviceAdapter::Parameters());
```

`MakeUSBHexStringDescriptor()` is in `Descriptor/USBHexStringDescriptor.h`; the MAC index is the one the function's layout gives, so the application does not number the strings itself.

## Function

`NcmFunction<InNtbSize, OutNtbSize, TxFrames, RxFrames>` is a `USBFunction`. The template parameters set the RAM it reserves:

| Parameter | Meaning | Default |
|-----------|---------|---------|
| `InNtbSize` | The largest NTB the device sends, dwNtbInMaxSize. 2048 to 65535 bytes | 2048 |
| `OutNtbSize` | The largest NTB the host sends, dwNtbOutMaxSize. 2048 to 65535 bytes | 2048 |
| `TxFrames` | Frames `SendFrame()` queues for the next IN block | 4 |
| `RxFrames` | Received frames waiting for `ReceiveFrame()` | 4 |

RAM: the buffers take `InNtbSize + OutNtbSize` bytes and `(TxFrames + RxFrames) * 1520` bytes of frame slots (1520 bytes a slot on the 32-bit target), plus 512 bytes of NTB builder tables. The object holds more than the buffers: the counters, the delegates, the control reply and notification buffers and the NTB parameters. Those are not in the figures below. The buffers and tables of the defaults take 16768 bytes; `2048, 2048, 2, 2` takes 10688 bytes.

| Method | Context | Description |
|--------|---------|-------------|
| `SendFrame(frame)` | Thread | Queues one frame, 14 to 1514 bytes. `notReady` while the data path is closed, `busy` when the queue is full or the call comes from interrupt context, `invalidParameter` for another size. The frame is copied |
| `NextFrameLength()` | Thread | Length of the oldest received frame, or 0 (also from interrupt context) |
| `ReceiveFrame(frame)` | Thread | Copies the oldest received frame and removes it. Returns its length, or 0 when none waits, `frame` is shorter, or the call comes from interrupt context; check `NextFrameLength()` first |
| `IsDataPathActive()` | Any | True while the host has alternate 1 selected |
| `SetLinkUp(bool)`, `IsLinkUp()` | Thread | The application's link state, up by default. A change while the path is open is sent by the next `Process()`. A change the host already holds sends nothing, so a change that is undone before `Process()` sends nothing either |
| `GetCounters()` | Thread | Frames received, dropped on receive, sent, dropped on send; NTBs received, malformed, sent, dropped stale (`ntbsDroppedStale`). An NTB of a session that ended before its parse, or while it ran, counts as dropped stale: it is not received, and no rule counts it. A frame of an NTB that never reached the host (withdrawn before the endpoint took it, or abandoned in flight by a session end) counts as dropped on send, not as sent. So do the queued frames a session end clears; the received frames it clears count as dropped on receive |
| `GetMalformedCount(status)` | Thread | NTBs that broke the given `USBNcmNtb16Status` rule |
| `GetPacketFilter()` | Any | The bitmap of the last SET_ETHERNET_PACKET_FILTER. Stored, not used for filtering |
| `MacStringIndex()` | Any | The run-time string index of the MAC address, for `USBDevice::SetRuntimeString()` |
| `SetDataPathStartedHandler()`, `SetDataPathStoppedHandler()` | Thread, before Start | `Callback::Delegate<void()>`. Started when alternate 1 opens the path; stopped when it closes, if it was open. Both run in interrupt context, so they are short, and they must not call `SendFrame()`, `ReceiveFrame()`, `NextFrameLength()` or other queue API. `busy` once configured |
| `SetFrameReceivedHandler()` | Thread, before Start | `Callback::Delegate<void()>`. Called from `Process()` after an NTB delivered at least one frame. `busy` once configured |

The class requests, on the communication interface. Anything else is stalled, and so is a request with the wrong direction or length:

| Request | bmRequestType | bRequest | Effect |
|---------|---------------|----------|--------|
| GET_NTB_PARAMETERS | 0xA1 | 0x80 | 28 bytes: NTB-16 only, both sizes at the template's limits, divisors and alignments 4, no datagram limit |
| GET_NTB_FORMAT | 0xA1 | 0x83 | 2 bytes, 0 (NTB-16) |
| SET_NTB_FORMAT | 0x21 | 0x84 | wValue 0 is accepted; 1 (NTB-32) is stalled |
| GET_NTB_INPUT_SIZE | 0xA1 | 0x85 | 8 bytes: the size, then the datagram count and a reserved word |
| SET_NTB_INPUT_SIZE | 0x21 | 0x86 | 4 or 8 bytes. A size from 2048 to `InNtbSize` is stored; any other size is stalled and the old size stays. The 8-byte form also sets the datagram limit (0 is no limit). The values stay until alternate 0, a configuration, a deconfiguration or a bus reset |
| SET_ETHERNET_PACKET_FILTER | 0x21 | 0x43 | wValue stored and acknowledged, no data |

Multicast filters, statistics, net address, CRC mode and the maximum datagram size are not advertised, and their requests are stalled.

### Alternate Settings

| Alternate | Effect |
|-----------|--------|
| 1 | Opens the bulk pair and starts a session: the NTB sequence restarts at 0, the queued frames are dropped, and the OUT endpoint is armed. The NTB input size, the datagram limit and the packet filter keep the values the host set. The notification endpoint sends CONNECTION_SPEED_CHANGE (12 Mbit/s both ways), then NETWORK_CONNECTION(1), or NETWORK_CONNECTION(0) when the link is down |
| 0 | Closes the bulk pair and drops the queued frames. Sends NETWORK_CONNECTION(0) only when the host holds the link up: the state of the last connection notification the host took, or of a connection in flight that says up. A notification the session queued and the host never took is dropped. The NTB input size and datagram limit go back to `InNtbSize` and no limit, and the packet filter to 0 |

The host sets the NTB input size on alternate 0 before it selects alternate 1 (Linux `cdc_ncm` does), so alternate 1 keeps the value.

A bus reset and a deconfiguration end the session the same way, without the notification, and reset the NTB input size, the datagram limit and the packet filter. The next configuration starts with the link down until the host selects alternate 1.

### Data Path

Receive. The host's NTB is taken into the OUT buffer. `Process()` checks it and queues its frames, then arms the OUT endpoint again. The endpoint NAKs the host until the buffer is parsed. An arm the endpoint refuses (busy, or halted) is tried again by every `Process()` until it takes. A block with any broken rule delivers nothing, and is counted by its rule (`GetMalformedCount()`). A frame that does not fit the receive queue is dropped and counted. A block the host sent in a session that has ended (alternate 0, a bus reset, a deconfiguration, or a new alternate 1 before `Process()`) is dropped and counted as dropped stale (`ntbsDroppedStale`), not as received. The frames a block queued before its session ended are counted as dropped on receive. The OUT endpoint is armed again in the new session at once.

Send. `Process()` packs the queued frames into one IN block, no larger than the host's NTB input size, and starts it when the IN endpoint is idle. Frames queued while a block is in flight wait for the next block, so they batch naturally. A block that is a whole number of 64-byte packets and smaller than the host's input size ends with a zero-length packet. A block of exactly the host's input size gets none: the host reads up to that size, so its read ends with the block. The block's sequence number counts from 0 in each session. The NCM block rule is kept as it is; the bulk pair of the other classes uses the rule of the [Vendor page](../Vendor/README.md#packets-and-transfers).

A block is built in `Process()`, not in the interrupt. Its frames leave the queue when the block is committed, and the input size and datagram limit are read with the session together, so a change of either during the build is seen. The block is committed only if the session is the same and the block fits the limits the host holds at that moment. A committed block that the endpoint has not taken yet (its endpoint halted, or the start was refused) is checked again before it starts. If the host lowered its input size under it, it is withdrawn: its frames are counted in `framesDroppedSend`, and its sequence number is given back. A block withdrawn by the end of a session is counted the same way, and so is a block the session ends while it is in flight: the endpoint abandons it with no completion, so the host never takes it.

When a block completes and no block is ready, the next one waits for the next `Process()` call. A call every loop keeps the endpoint busy.

Context:

| Code | Context | Rule |
|------|---------|------|
| `OnTransferComplete`, `OnEndpointHaltChanged`, `OnSetInterface`, `OnConfigured`, `OnDeconfigured`, `OnBusReset`, `OnControlRequest`, `OnControlDataReceived` | Interrupt | Short. They start transfers and set flags. They never parse, pack or copy frames |
| `SendFrame()`, `ReceiveFrame()`, `NextFrameLength()`, `SetLinkUp()`, `Process()` | Thread | The queues are touched by the thread only. The interrupt asks for a session reset with a flag, and the thread applies it before it reads a queue. `SendFrame()` returns `busy`, `ReceiveFrame()` and `NextFrameLength()` return 0 when called from interrupt context (`System::IsInterrupt()`), so a handler that calls them by mistake changes nothing |
| The state the interrupt reads | Both | Read and written inside `System::CriticalSection` when the thread side changes it. The blocks are built outside the sections |

Limits and choices:

- Frames are 14 to 1514 bytes, without FCS. NTB-16 only; CRC datagrams (`NCM1`) are refused.
- A block received before the path closed is dropped, even when its parse had not yet run. Each session has a generation, and a block belongs to the one it arrived in.
- A session end clears the queued frames of both directions at the next thread call, and counts them (`framesDroppedSend`, `framesDroppedReceive`). No thread call reads a queue while a session change is pending, so a frame queued in one session is never sent in the next.
- A block that the host's lowered input size no longer fits, while the IN endpoint is halted, is withdrawn by the first `Process()` after the halt clears, not while the halt lasts. Its frames are counted as dropped on send. They left the queue when the block was committed. That is the rule, not a leak; the counts show it.
- The link bit rate in CONNECTION_SPEED_CHANGE is 12 Mbit/s in both directions. The host shows the speed; it does not pace the data.
- A block holds at most 128 frames (the builder's table, `MaxDatagramsPerNtb`).

### Host Support

Not tested on hardware or on a real host. The intended drivers are the in-box ones that bind by class code: `UsbNcm.sys` on Windows 10 2004 and later, `cdc_ncm` on Linux, and the macOS in-box driver. The simulator tests check the bytes the host reads and the requests it sends.

## Tests

The host tests are in `.tool/sim` and run with `run.sh`. The serial port's tests, the bootloader transport and the CDC builder errors are on the [CDC Class page](README.md#tests).

- `TestNcmDescriptors.cpp`: the NCM device and configuration bytes with automatic and explicit addresses and a 4 ms interval, the functional descriptors, the MAC string from the helper, and the notification helpers, against arrays written by hand from NCM 1.0 and ECM 1.2.
- `TestNcmRequests.cpp`: each NCM request with its bytes, the NTB input size limits in both forms, the packet filter, the stalls and the lengths.
- `TestNcmAlternates.cpp`: alternates 1 and 0 opening and closing the endpoints, GET_INTERFACE, the notification order and bytes (speed change, then connection), the link down and up, a link toggle with the host holding the link up (nothing sent), two link changes while a notification is in flight, a close before the host saw the link up (no disconnect), a halt of the notification endpoint with the speed change or the connection in flight (sent again after the clear), a close after a halted connection that the host never took (no disconnect), a close with a connection in flight that the host takes afterwards (the disconnect follows it), deconfiguration and a bus reset in a session, the SendFrame states, and the busy handler setters.
- `TestNcmSession.cpp`: a block of a closed session that is parsed after the session changed (alternate 1, 0, 1; a bus reset; a deconfiguration) is dropped and counted as `ntbsDroppedStale`, not as received, and the OUT endpoint is armed again; the frames and the NTB in flight of a closed session go with it, and an NTB in flight at a close is counted as dropped; an OUT halt holds the arm until the clear; a session change inside the `Process()` that starts an IN transfer does not send the frame queued before it into the new session; the frames a session end clears are counted in both directions; a refused OUT arm is retried by `Process()`; the queue API refuses from interrupt context (`System::IsInterrupt()`, simulated with `simInterruptContext`).
- `TestNcmInput.cpp`: the host's NTB input size on a function that advertises 4096 bytes: a size lowered before packing, a size lowered while the IN endpoint is halted with the block waiting (the block is withdrawn, not sent, and its frames are counted as dropped), a block that fits after the halt (sent once), a block withdrawn by the end of a session, and the datagram limit of the 8-byte form.
- `TestNcmParameters.cpp`: the input size, the datagram limit and the packet filter across alternate 1 (kept), alternate 0, a configuration and a bus reset (reset).
- `TestNcmData.cpp`: blocks in both directions with byte-exact expected NTBs written by hand (two frames, one frame, a whole-packet block with its zero-length packet), the sequence restart with the session, the input size limit, frames queued while a block is in flight, malformed blocks counted by rule with the next good block delivered, a full receive queue, a chain of two NDPs, and a zero-length OUT transfer.
- `TestNcmNtb16.cpp`: the NTB-16 codec on its own: the parameter structure, the parser's rules (including the blocks of 12 to 15 bytes, which have no room for an NDP), the builder's layout and padding, and round trips.
- `BuilderErrors/ncm_*.cpp`: the compile errors of the NCM builder, each with the message it must give.
