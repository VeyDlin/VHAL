# VUB::CdcLink

A byte-stream link over a CDC-ACM function, which is a USB serial port. Header: `Common/Drivers/Interface/VUB/Link/CdcLink.h`.

```cpp
template<typename Cdc, std::size_t MaxPacketBytes = 64>
class CdcLink;
```

`Cdc` is the function class: `CdcAcmFunction<TxBufferSize, RxBufferSize, PacketsPerTransfer>`, or any class with the same members.
`MaxPacketBytes` is the largest packet of the link, 64 by default.

## Traits

- Stream: a framing goes above the link.
- Reliable: the USB transfers arrive in order and without loss while the device is configured.
- Full duplex.
- Either side starts.
- Reports its state: `ReportsState()`. The stack needs the link's `SetStateSink`, and `LinkUp()` on the stack follows the link.
- `MaxPacket(MaxPacketBytes)`.

## Device API

| Member | Used by | Context |
|---|---|---|
| `Write(std::span<const uint8>)` | `Write` | main (`Process`) |
| `Read(std::span<uint8>)` | `Poll` | main (`Process`) |
| `IsConfigured()` | the state | main (`Process`) |
| `IsTerminalOpen()` | the state (DTR) | main (`Process`) |

`Write` queues what the TX ring takes and returns the count, which is less than asked when the ring is full, and 0 while the function is
not configured. `Read` copies what the RX ring holds, and returns 0 while the function is not configured.

## State

The link is up while the function is configured and the host has DTR set, which is the signal of an open terminal. A host opens a serial
port with DTR set, so the link comes up when the port is opened and goes down when the port is closed or the device is deconfigured.

`SetStateSink` gives the link the sink of its stack. The link reports its state at once, then only when it changes. The state is sampled
in `Poll`, so a change reaches the stack at the next `Process`, which polls first.

## Receiving

`Poll(receiver)` runs in `Process`, before the stack takes its received packets. It samples the state, then reads from the function up
to `receiver.Room()`, in pieces of 64 bytes, and feeds each piece with `Feed`. It stops when the room is used up or the function has no
more bytes. A byte the room does not have stays in the function's RX ring, so the link drops nothing.

## Transmitting

`Write(bytes)` is the function's `Write`. It returns the count the ring took. A count less than `bytes.size()` means the rest is kept by the
stack, which writes it again on the next `Process`.

## Contexts

- `SetStateSink`, `Poll` and `Write`: main context, from the stack's constructor and from `Process`. The function's ring is safe from
  the interrupt on its own (its own critical sections), so the link needs no lock.
- The state is read in `Poll`, not in the function's interrupt. A change of DTR is seen at the next `Process`.

## Snippet

The snippet is compiled in `TestVubLinkWiki.cpp` with the real `CdcAcmFunction<512, 256, 2>` and the descriptor set of the USB test devices,
so `usbCdc` is a function of the real class.

```cpp
// The device owns its CDC function: a 512-byte TX ring, a 256-byte RX ring and 128-byte transfers. The link refers to it.
using CdcFunction = CdcAcmFunction<512, 256, 2>;

void OnHostPayload(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
}

using HostCdc = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<VUB::CdcLink<CdcFunction>>()
		.MaxPayload(32)
		.OnReceive<&OnHostPayload>();
}>;

// usbCdc is the device's CDC function, made from the USB descriptors. The stack and the link refer to it.
VUB::CdcLink<CdcFunction> cdcLink(usbCdc);
HostCdc cdcStack(cdcLink);

// The main loop calls Process: the stack polls the link, which reads the host's bytes, and then sends what is queued.
void CdcLoop() {
	cdcStack.Process();
}
```

## Size

Measured on the host (`sizeof`, g++ 15 on Windows, x86-64) with the real class: `CdcLink<CdcAcmFunction<512, 256, 2>>` is 96 bytes. The function
itself is 1400 bytes and is the device's, not the link's. The link holds the function pointer, the state sink, the state flag and a 64-byte
read buffer.

## Tests

`TestVubCdcLink.cpp` runs the link over `FakeCdc`, with the members of the real class: the writes and the full ring, the state through
configuration and DTR, `Poll` reading up to the room, the stack's `LinkUp()` and `LinkDowns()`, and two stacks with COBS and with
`VUB::Reliable` over two functions sharing their queues. `TestVubLinkUsb.cpp` runs the link over the real `CdcAcmFunction` on the simulated
controller: the host sets DTR, writes a COBS frame to the bulk OUT endpoint, and reads the device's reply from the bulk IN endpoint byte by
byte.
