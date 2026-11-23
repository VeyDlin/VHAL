# VUB::BulkLink

A byte-stream link over a vendor bulk function in buffered mode: a pair of bulk endpoints that the application reads and writes as a
byte stream. Header: `Common/Drivers/Interface/VUB/Link/BulkLink.h`.

```cpp
template<typename Vendor, std::size_t MaxPacketBytes = 64>
class BulkLink;
```

`Vendor` is the function class: `VendorBulkFunction<TxBufferSize, RxBufferSize, PacketsPerTransfer>` in buffered mode, or any class with the
same members. `MaxPacketBytes` is the largest packet of the link, 64 by default.

## Traits

Identical to [CdcLink](CdcLink), except for the state:

- Stream, reliable, full duplex, either side starts, `MaxPacket(MaxPacketBytes)`.
- Reports its state: `ReportsState()`.

## Device API

| Member | Used by | Context |
|---|---|---|
| `Write(std::span<const uint8>)` | `Write` | main (`Process`) |
| `Read(std::span<uint8>)` | `Poll` | main (`Process`) |
| `IsConfigured()` | the state | main (`Process`) |

A vendor function has no DTR line, so the state is only whether the function is configured. The link is up as soon as the host configures
the device, and down when the device is deconfigured or the bus is reset.

## Receiving and transmitting

Both work as on [CdcLink](CdcLink): `Poll` samples the state, reads up to the receiver's `Room()` in pieces of 64 bytes, and feeds them;
`Write` is the function's `Write`, which queues what the ring takes and returns 0 while the ring is full or the function is not configured.
The stack keeps what is not taken and writes it on the next `Process`.

In buffered mode the function is a byte stream: the transfer boundaries of the host's writes are not kept. The link does not depend on
them, and a framing above the link (COBS, length prefix or gap framing) gives the packets their edges.

## Contexts

As [CdcLink](CdcLink): `SetStateSink`, `Poll` and `Write` run in main context, and the function's rings are safe from the interrupt.

## Snippet

The snippet is compiled in `TestVubLinkWiki.cpp` with the real `VendorBulkFunction<512, 256, 2>`, so `usbVendor` is a function of the real
class.

```cpp
// The device's vendor bulk function: a 512-byte TX ring, a 256-byte RX ring and 128-byte transfers.
using VendorFunction = VendorBulkFunction<512, 256, 2>;

void OnVendorPayload(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
}

using HostVendor = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<VUB::BulkLink<VendorFunction>>()
		.MaxPayload(32)
		.OnReceive<&OnVendorPayload>();
}>;

// usbVendor is the device's vendor bulk function, made from the USB descriptors. The stack and the link refer to it.
VUB::BulkLink<VendorFunction> bulkLink(usbVendor);
HostVendor vendorStack(bulkLink);

// The main loop calls Process: the stack polls the link and then sends what is queued.
void BulkLoop() {
	vendorStack.Process();
}
```

## Size

Measured on the host (`sizeof`, g++ 15 on Windows, x86-64) with the real class: `BulkLink<VendorBulkFunction<512, 256, 2>>` is 96 bytes. The
function is 1304 bytes and is the device's. The link holds the function pointer, the state sink, the state flag and the 64-byte read buffer.

## Tests

`TestVubBulkLink.cpp` runs the link over `FakeBulk`: the state through configuration, `Poll` up to the room, the full ring, the stack
over two functions with COBS and with `VUB::Reliable`, with partial writes. `TestVubLinkUsb.cpp` runs it over the real
`VendorBulkFunction` on the simulated controller.
