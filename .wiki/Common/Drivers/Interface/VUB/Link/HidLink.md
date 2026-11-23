# VUB::HidLink

A packet link over a HID function: each report is one packet, with its length in the report. Header: `Common/Drivers/Interface/VUB/Link/HidLink.h`.

```cpp
template<typename Hid, std::size_t ReportBytes = 64>
class HidLink;
```

`Hid` is the function class: `HidFunction<QueueDepth>`, or any class with the same members. `ReportBytes` is the length of the input and
output reports of the interface, which the function's report descriptor sets, and it is at most 256. The packet link's largest packet is
`ReportBytes - 1`, the report without its length byte.

## Traits

- Packet, framed by its report: no framing goes above the link. A framing above a packet link is a builder error.
- Not reliable: the host may drop an input report it does not read. Put [VUB::Reliable](../Layer/Reliable) above the link when a packet must
  arrive.
- Full duplex. Either side starts.
- `MaxPacket(ReportBytes - 1)`.
- No state report: the link is always up. Before the device is configured, `Send` returns busy, so the stack holds the packets.

## The report layout

A report is `ReportBytes` long and has no report ID. The layout is the one `HidReportPack` writes and `HidReportPayload` reads:

| Byte | Content |
|---|---|
| 0 | the payload's length, from 1 to `ReportBytes - 1` |
| 1 to n | the payload, n bytes, where n is byte 0 |
| n + 1 to `ReportBytes - 1` | zero |

A three-byte packet `A1 B2 C3` in a 64-byte report is `03 A1 B2 C3` followed by 60 zero bytes. The receiving side reads byte 0 and takes the
next n bytes; the padding is not checked. The report descriptor of the interface must have no report IDs, and its input and output reports
must be `ReportBytes` long.

## Device API

| Member | Used by | Context |
|---|---|---|
| `SendReport(std::span<const uint8>)` | `Send` | main (`Process`) |
| `SetOutputDelegate(OutputDelegate)` | `SetReceiver` | main, before the device starts |
| `OutputDelegate` (`VSTD::Delegate<void(std::span<const uint8>)>`) | the output reports | interrupt |

`SendReport` takes exactly `ReportBytes` bytes and returns `invalidParameter` otherwise, `notReady` before the device is configured, and
`busy` when the queue of input reports is full. `SetOutputDelegate` returns `busy` once the device is configured, so a link that is given its
receiver after that keeps the status in `ReceiveStatus()`.

## Sending

`Send(packet)` packs the packet into the link's report buffer (`HidReportPack`) and calls `SendReport` with the whole report. Its result:

- `ok`: the report is queued.
- `busy`, and `notReady` (the device is not configured yet): `busy`, so the stack keeps the packet and sends it on a later `Process`.
- `invalidParameter`: an empty packet, or one longer than `ReportBytes - 1`. The stack counts it as a send error.
- Any other status from the function is returned as it is.

## Receiving

`SetReceiver(receiver)` with an attached receiver sets the function's output delegate to the link. The function calls it in its
interrupt for each output report, and the link decodes the report (`HidReportPayload`) and feeds the payload to the receiver with
`FeedPacket`. A report whose length byte is 0, or larger than `ReportBytes - 1`, is dropped without a count.

`SetReceiver` with an empty receiver clears the output delegate, so no report reaches the link after the stack is gone.

## Contexts

- `SetReceiver`, `Send` and `ReceiveStatus`: main context, the stack's constructor and `Process`.
- The output delegate: the function's interrupt. It feeds the receiver, which is safe from an interrupt.

## Snippet

The snippet is compiled in `TestVubLinkWiki.cpp` with the real `HidFunction<8>` and a 64-byte report descriptor (the test's own, made with
`USBHidReportBuilder::VendorRaw(64)`), so `usbHid` is a function of the real class with the report size of the link.

```cpp
// The device's HID function: 64-byte input and output reports, and a queue of 8 input reports.
using HidFunctionType = HidFunction<8>;

void OnRemotePayload(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
}

// No framing: each report is one packet. The queue of received packets holds 3.
using Remote = VUB::Stack<+[] {
	return VUB::Builder()
		.Link<VUB::HidLink<HidFunctionType, 64>>()
		.MaxPayload(63)
		.ReceiveQueue(4)
		.OnReceive<&OnRemotePayload>();
}>;

// usbHid is the device's HID function, made from the USB descriptors. The stack and the link refer to it.
VUB::HidLink<HidFunctionType, 64> hidLink(usbHid);
Remote remoteStack(hidLink);

// The main loop calls Process: the stack sends the packets it has, and the function sends the reports queued.
void HidLoop() {
	remoteStack.Process();
}
```

## Size

Measured on the host (`sizeof`, g++ 15 on Windows, x86-64): `HidLink<HidFunction<8>, 64>` is 112 bytes, and `HidLink<HidFunction<8>, 8>`
is 56 bytes. The function (`HidFunction<8>`, 1896 bytes) is the device's. The link holds the function pointer, the receiver, the report buffer
and the status of the last `SetReceiver`, so its report buffer is `ReportBytes` bytes.

## Tests

`TestVubHidLink.cpp` runs the link over `FakeHid`, which has the function's `SendReport`, its refusals and its output delegate. It checks the
report bytes, the refusals, busy and not-ready, the output delivery and the drops, and two stacks with and without `VUB::Reliable` over two
functions with busy reports, which keep packets in the stack. `TestVubLinkUsb.cpp` runs the link over the real `HidFunction`: the host
reads the report with `HostIn` and writes an output report with `HostOut`.
