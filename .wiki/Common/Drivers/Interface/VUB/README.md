# VUB

VUB is a protocol stack built from blocks. A product's protocol is a chain of blocks, written in one builder and checked at compile
time. Each block has a small contract. The built-in blocks use only that contract, so a user block goes into the chain the same way.

VUB has the core: the five block contracts, the builder and its checks, the stack at run time, drop counters, a test kit, and the
built-in blocks: the CRC transform ([CRC](Transform/Crc)), the framings ([COBS](Framing/Cobs),
[length prefix](Framing/LengthPrefix) and [gap framing](Framing/Gap)), the delivery layer
([VUB::Reliable](Layer/Reliable)), and the keep-alive layer for a link that cannot report its state
([VUB::KeepAlive](Layer/KeepAlive)). A chain is made of these, of user blocks, and of the test kit's loopback.

The links are built for a microcontroller ([links](Link/README)): a UART, a USB CDC serial port, a USB vendor bulk interface and a USB
HID interface, each a template over the VHAL adapter or USB function. On a PC ([PC links](PC/README)): a serial port, TCP, USB bulk and
HID, on Windows and Linux, so the PC side of a product uses the same blocks. The [board check](.tool/board/README) runs each link between
an STM32G431 and a PC.

## The chain

A chain is written from the application down to the link:

```cpp
using Board = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<SumCheck>()
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>();
}>;
```

The application sends into the top, and the link is the last block. A block that is not in the chain has no code and no RAM.

## Names

Every type of VUB is in the namespace `VUB`, and the pages name each type with it: `VUB::Stack`, `VUB::Reliable`,
`VUB::Crc16Modbus`. Inside the builder's lambda, `using namespace VUB;` shortens the chain. Write it there or in a `.cpp` file, not in
a header, so the short names do not reach the files that include the header:

```cpp
using Board = VUB::Stack<+[] {
	using namespace VUB;
	return Builder()
		.Layer<Reliable>().Retries(3).TimeoutMs(200)
		.Transform<Crc16Modbus>()
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>()
		.Clock<&BoardClockUs>();
}>;
```

`Crc` is also the class of the shared CRC library (`Utilities/Checksum/CRC/Crc.h`). In a file that includes both, the short name
`Crc` is ambiguous, so the transform is written `VUB::Crc`. The presets, such as `Crc16Modbus`, have no such clash.

## The five kinds

| Kind | Concept | What it does | Members the concept needs |
|---|---|---|---|
| Link | `VUB::StreamLink`, `VUB::PacketLink` | the wire at the bottom | `traits`; `Write(span)` returning `Result<size_t>` (a byte stream), or `Send(span)` returning `ResultStatus` (whole packets). Optional, not both: `SetReceiver(Receiver)` for a link that receives in an interrupt (`VUB::ReceivingLink`), or `Poll(Receiver)` for a link that is polled from `Process` (`VUB::PollingLink`); see [receiving from a link](#receiving-from-a-link) |
| Framing | `VUB::FramingBlock` | finds packet boundaries in a byte stream | `usesTime`, `Growth(length)`, `Frame(meta, packet, out)`, `Push(byte, out, length)`; with `usesTime`: `Push(byte, nowUs, out, length)`, `Poll(nowUs, out, length)`, `Ready(nowUs)` and `Sent(nowUs, count)` (see [gap framing](Framing/Gap)) |
| Transform | `VUB::TransformBlock` | one packet into another, and back | `growth`, `fingerprint`, `Encode(meta, in, out)`, `Decode(meta, packet)` |
| Engine | `VUB::EngineBlock`, `VUB::EngineFor` | computes what a transform describes (a CRC, AES) | `synchronous`; `Supports<Algorithm>()` and `Run<Algorithm>(data)` for `VUB::EngineFor` (see [engines](Engine/README)) |
| Layer | `VUB::LayerBlock` | a header, state, time; may send packets of its own and make bulk packets | `header`, `fingerprint`, `usesTime`, `makesBulk`, `Add`, `Take`, `Process`, and `Next` when `makesBulk` is true |

A block that misses a member is a compile error. The message names the concept it does not satisfy, for example `the transform does not satisfy VUB::TransformBlock`, and the concept's notes follow it.

The link has two optional members, and the stack has no code for one it does not use. A link with `SetReceiver` feeds its data from an interrupt; a link with `Poll` is polled for its data by `Process`. A link with both is a compile error (see [receiving from a link](#receiving-from-a-link)).

## The builder

`VUB::Builder()` makes an empty builder. Each method that adds a block returns a builder with one more block in its type. The four
block methods take the block's type, and optionally an options value:

- `.Layer<T>()` or `.Layer<T>(options)`
- `.Transform<T>()` or `.Transform<T>(options)`
- `.Framing<T>()` or `.Framing<T>(options)`
- `.Link<T>()` (the link takes no options; its object is built by the user)

The options value of a block is passed to the block's constructor when the stack is built, unless the block declares `Configured` (see
[writing a block, options](#options)). Then the stack stores the block with the options as template arguments, and the block needs no
constructor. A block given options that does not declare `Configured` must have a constructor that takes them; otherwise the chain does
not compile. A block given no options is default-constructed.

Settings are set anywhere in the chain:

| Method | Meaning | Default |
|---|---|---|
| `MaxPayload(bytes)` | the largest application payload | required |
| `ReceiveRing(bytes)` | bytes the receive ring holds (byte-stream links) | 256 |
| `ReceiveQueue(packets)` | packets the receive queue holds (packet links) | 4 |
| `SendQueue(packets)` | packets held for the link while it is busy | 4 |
| `PerProcess(packets)` | most packets one `Process` call sends | 8 |
| `Clock<&Fn>()` | the clock, a function returning microseconds as `uint32` | required when a block uses time |
| `OnReceive<&Fn>()` | the receive handler, `void(const VUB::Meta&, std::span<const uint8>)` | required |
| `WhileDown(policy)` | what the packets do while the connection is down: `VUB::Down::Wait` or `VUB::Down::Fail` (see [Link state](#link-state)) | `Wait` |

The builder checks: one link, the link last; a byte-stream link has one framing right above it, a packet link has none; a link has
traits with `Framed()` only when it is a packet link, `HalfDuplex()` or `FullDuplex()`, and a non-zero `MaxPacket(n)`; the packet built
from the payload, the headers and the growths fits the link's `MaxPacket`; the sizes and the handler are given; a block that uses time
has a clock. The lengths and indexes are 16-bit: a packet, a framed packet and a link's `MaxPacket` are at most 65535 bytes, and the
ring and the queues hold at most 65535 entries. Each rule is a compile error with its message. A link with `ReportsState()` must take `SetStateSink`, and `WhileDown(VUB::Down::Fail)` needs
something that can report the connection down. A link with both `SetReceiver` and `Poll` is a compile error (see [receiving from a link](#receiving-from-a-link)).

Options after a block: `LastIs<Block>()`, `LastOptions()` and `WithLastOptions(value)` read and replace the options of the last block.
A built-in block that has options after it (for example `.Reliable().Retries(3)`) is a method on the builder that checks `LastIs` and
calls `WithLastOptions`. A user block does not need this.

## Send and receive

`Send(payload, meta)` encodes at once: the payload goes through the layers and transforms, from the top down, and the result is put
in the control queue. `Process()` does the rest, in this order:

1. reads the clock (`Process` reads it, and so does `Feed` in a chain whose framing uses time, see below);
2. polls the link, when the link has `Poll` (see [receiving from a link](#receiving-from-a-link));
3. takes the received bytes or packets, and decodes them from the bottom up: framing, transforms and layers (each layer removes its
   header), then the handler;
4. calls every layer's `Process(now, outbox)`, which may queue packets of its own, or pass a packet up (see [passing a packet up](#passing-a-packet-up-from-process));
5. sends.

`Feed(byte)` (byte-stream link) and `FeedPacket(packet)` (packet link) are called from the receive interrupt. They store the data;
everything else runs in `Process`. A chain whose framing uses time stores the time of each byte in `Feed`, so the clock is read there too.

`Find<Block>()` returns the block of that type in the chain, for the settings that change at run time, such as the gap framing's baud
rate: `stack.Find<VUB::Gap>().SetBaud(19200)`. A chain without that block does not compile the call. A chain with two blocks of that type
is allowed, but `Find` does not compile for it: it cannot tell the two apart, and the message says so.

## Receiving from a link

A link hands its received data to the stack in one of three ways. The stack has code only for the way its link uses:

- `SetReceiver(VUB::Receiver)`: the link receives in an interrupt or another thread. The stack gives the link a receiver in its
  constructor and a null receiver in its destructor, so the link does not feed a stack that is gone; a stack with such a link is not
  copyable. The link calls `receiver.Feed(bytes)` (a byte-stream link) or `receiver.FeedPacket(packet)` (a packet link) from its
  interrupt. A UART is this kind.
- `Poll(VUB::Receiver)`: the link reads on request. `Process` calls `Poll` first, ahead of taking the received bytes or packets, so
  what the link feeds is handled in the same `Process`. `Poll` must not block, and it feeds no more than `receiver.Room()`. A USB class
  or a TCP connection is this kind.
- Neither: the application feeds the stack itself, with `Feed(byte)` or `FeedPacket(packet)`, from the receive interrupt or ahead of
  `Process`. Such a link has no receiver and no code for one.

A link with both `SetReceiver` and `Poll` is a compile error: two writers would share the receive ring or queue.

`Room()` says how much the stack takes at the time of the call: the free bytes of the receive ring of a byte-stream link, or the free slots of the receive
queue of a packet link. A byte or a packet that does not fit is dropped and counted as `Feed` and `FeedPacket` count it (`ringFull`,
`queueFull`, `tooLong`). A receiver with no stack behind it, a default one or the one a destroyed stack left in its link, has no room
and drops what it is given.

The snippet below has an interrupt link and a polled link. The interrupt feeds one COBS frame, `02 01 00`, one byte at a time, and
`Process` hands up its packet. The polled link has three packets and its queue holds two, so the first `Process` polls two of them
and takes them at once, and the third waits for the next call. The sample is in the test of the wiki snippets, with its checks.

```cpp
// A byte-stream link that receives in an interrupt. The stack gives it a receiver, and the interrupt feeds the receiver a byte at a time.
struct UartLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().MaxPacket(64).HalfDuplex().Starts(VUB::Initiator::Either);

	// The stack gives its receiver here, and takes it back with a null receiver when it is destroyed.
	void SetReceiver(VUB::Receiver value) {
		receiver = value;
	}

	Result<std::size_t> Write(std::span<const uint8> bytes) {
		return bytes.size();
	}

	// Called from the receive interrupt. A byte that the ring has no room for is dropped and counted under ringFull.
	void OnByte(uint8 byte) {
		receiver.Feed(std::span<const uint8>(&byte, 1));
	}

	VUB::Receiver receiver;
};


// A packet link that is polled. Poll feeds the packets it has while the receive queue has room for them.
struct PollLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex();

	// Called from Process, ahead of the stack taking its received packets. Room is the number of free slots of the receive queue.
	void Poll(VUB::Receiver receiver) {
		while (packetsWaiting > 0 && receiver.Room() > 0) {
			std::array<uint8, 2> packet = {0x01, 0x02};
			receiver.FeedPacket(packet);
			packetsWaiting--;
		}
	}

	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}

	std::size_t packetsWaiting = 0;
};


std::size_t receivedCount = 0;


void OnReceived(const VUB::Meta&, std::span<const uint8>) {
	receivedCount++;
}


using Uart = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<UartLink>()
		.MaxPayload(32)
		.ReceiveRing(64)
		.OnReceive<&OnReceived>();
}>;


using Polled = VUB::Stack<+[] {
	return VUB::Builder()
		.Link<PollLink>()
		.MaxPayload(32)
		.ReceiveQueue(2)
		.OnReceive<&OnReceived>();
}>;


UartLink uartLink;
Uart uart(uartLink);
PollLink pollLink;
Polled polled(pollLink);
```

A chain whose link has neither member is the same size as a chain without the receiver. Measured with `sizeof` on g++ (x86-64), the
packet stack of the link with neither member is 656 bytes and the stream stack 952 bytes (`TestVubReceiver.cpp` checks both). With
`size.sh`, every row of the [size table](#size) is unchanged.

## Backpressure and order

A link may refuse data for a while: a USB class takes only part of a write, a transmitter is busy. The stack never drops an outgoing
packet for that reason.

- A byte-stream link's `Write` returns how many bytes it took. The stack keeps the rest of the framed packet and writes it on the next
  `Process`.
- A packet link's `Send` returns `ok` when it took the packet, and `busy` when it has no room for it. The stack keeps the packet and
  tries again on the next `Process`. Any other status is an error: the packet is dropped and counted as `sendError`.
- `Process` keeps sending while the link accepts, up to `PerProcess` packets, so one call can move many packets. A packet that the
  framing or the link refuses with an error counts toward `PerProcess` as well, so a bulk layer that always has data cannot hold a call.
- `Send` and the layers' `Process` queue packets on the control lane, up to `SendQueue` packets. When the queue is full, `Send` returns
  `busy` and nothing is queued; the caller sends again.
- In a chain with a layer that reports its peer, a layer's own packets (from its `Process` or its outbox) have a second queue of the same
  size, the own lane. The outbox's `Send` returns `busy` when that queue is full, and the layer keeps the packet and sends it again on the
  next `Process`. Any other chain puts the layers' packets on the control lane.

Order: in a chain with a layer that reports its peer, the layers' own packets go first, on the own lane. Then the control packets from the
application go, and then the bulk packets. In any other chain the layers' packets are control packets, so control packets (from the
application and from the layers' `Process`) go ahead of bulk packets. A layer that makes bulk packets (`makesBulk`) is asked for its next
packet through `Next` only when the link is free and no control packet waits. The stack asks the layers in chain order and takes the first
one that has data. The packet from `Next` is complete at the layer's level: the layer added its own header, and the blocks below add theirs.

## Link state

A link may report that it is up or down: a USB device when it is configured, a BLE link when it is connected. The link declares
`ReportsState()` in its traits and reports through the sink the stack gives it. A link that cannot tell is always up, and a chain that
cannot see a down has none of this: no record, no `LinkUp()`, no code.

A layer may also declare its peer down or up from `Process`, with `outbox.SetPeer(up)`, when it has a keep-alive of its own. The
stack treats it as a change of the connection. The keep-alive for a link that cannot report is [VUB::KeepAlive](Layer/KeepAlive).

A layer that declares `reportsPeer` must be the lowest layer of the chain: no other layer is below it, though transforms and the framing
may be. A chain holds one such layer. The builder refuses a reporting layer with another layer below it, with the message "a layer that
declares reportsPeer must be the lowest layer of the chain", and a second reporting layer, with "two layers declare reportsPeer". The
reason for the first: the layer's pings go through the layers below it. Above a delivery layer such as [VUB::Reliable](Layer/Reliable),
the pings go through a layer that holds back sending while the connection is down, so after a lost first session neither end can send its
ping, and both wait. Below it, the pings go out, and the delivery layer sees only the application's packets. The reason for the second: the
stack keeps one peer state, and the last `SetPeer` in a `Process` would overwrite the first.

A layer that declares `usesLinkState` asks the physical link's state from `Process` with `outbox.LinkUp()`. On a link that does not report
its state the answer is always true. A chain whose layers do not ask is unchanged.

- The connection is up when the link is up and no layer has declared the peer down. `LinkUp()` says so as of the last `Process`, and
  `LinkDowns()` counts the falls from up to down. Both exist only when the link reports or a layer reports its peer.
- The interrupt only records the report, and `Process` acts on it. A link that goes down and up between two calls of `Process` is still
  a down, and it is counted.
- On a change, `Process` does these, in this order: the framing's partial frame is reset (`Reset()`, when the framing has it); on a
  down, the bytes in the receive ring are discarded and counted under `linkDown`, and the frame being written is dropped (`Fail`) or
  starts again from its first byte (`Wait`); each layer with an `OnLink(up, now)` is told, in chain order.
- A peer that is down is not a link that is down. While the link is up and only the peer is down, the layers' own packets (from their
  `Process` or their outbox) still go out, so a layer can ping the peer and the peer can come back. The application's packets and the
  bulk packets wait. While the link is down, nothing goes out, the layers' packets included. Whole packets in the receive queue still go
  up in every case.
- A layer's own packets go ahead of the application's queued packets, in a chain with a layer that reports its peer. Any other chain has
  one queue for both, in the order they were queued.
- A frame that is partly written when the connection goes down starts again from its first byte, under `Wait` and under `Fail`. Under
  `Fail` an application's frame is dropped instead, and counted under `linkDown`; so is a layer's own frame while the link is down. A
  layer's own frame that starts again while only the peer is down goes on while the peer is down. Under `Wait`, while the peer is down, an
  application's frame is not sent: it waits, and goes from its first byte when the peer is up.
- `WhileDown` sets what happens to the packets waiting to be sent:
  - `Wait` (the default): they stay queued. `Send` queues until the queue is full, then returns `busy`, and the packets go in order
    after the connection is up. A layer's own packet waits the same way while the link is down.
  - `Fail`: the queued packets are dropped and counted under `linkDown`, and `Send` returns `noConnection` while the connection is
    down. When the peer goes down, the application's queued packets are dropped and the layers' own packets are kept; when the link goes
    down, the layers' own packets are dropped too. A layer's own packet gets `noConnection` only while the link is down. A refused
    `Send` is not counted: the caller has the status.
- `WhileDown(VUB::Down::Fail)` needs something that can report the connection down: a link with `ReportsState()`, or a layer with
  `reportsPeer`. Otherwise it is a compile error.
- Cost. A chain with a layer that reports its peer has a second send queue for the layers' own packets, of `SendQueue(n) + 1` slots of
  the packet size, with its lengths, flags and indexes, and a link record when the link does not report. Measured with `sizeof` on the
  Cortex-M4 for one layer that reports its peer (the same layer type with `reportsPeer` cleared and set), on a packet link that does not
  report, with `MaxPayload(40)` and the default queues: `sizeof` is 756 bytes without the report and 996 with it, so 240 bytes; `text`
  adds 230 bytes and `bss` 240. The method and the other figures are on the [keep-alive page](Layer/KeepAlive#cost). A chain without a
  layer that reports its peer has no second queue, and its size does not change.

The test kit can take a line down and up, see the [test kit](TestKit/README).

## Flags

`VUB::Meta` has a `flags` field. The block that makes a packet sets it: the application through the `meta` argument of `Send`, a layer
in `Next`, a layer in `Process` through the outbox. Every block below the producer reads it through the `const VUB::Meta&` it is given:
the layers' `Add`, the transforms' `Encode` and `Decode`, and the framing's `Frame`. Links do not get it: a link carries bytes or
packets, not flags. `VUB::Meta::NoAcknowledgeFlag` is defined, and the delivery layer honours it: a packet with the flag has no sequence
number and is not acknowledged (see [VUB::Reliable](Layer/Reliable)).

## Drop counters

Every packet or byte the stack discards is counted under one reason. `Drops()` returns a copy of the counters:

| Field | A packet or byte dropped because |
|---|---|
| `ringFull` | the receive interrupt found the ring full (byte-stream link) |
| `queueFull` | the receive interrupt found the queue full (packet link) |
| `tooLong` | a frame or packet did not fit in the receive buffer (the link's `MaxPacket`) |
| `framingError` | the bytes did not form a frame, or the frame's checks failed |
| `transformError` | a transform's `Decode` returned an error, or its `Encode` failed for a packet with no caller (see below) |
| `layerError` | a layer's `Take` returned an error, or a layer's `Add` or `Next` failed for a packet with no caller, or a layer's packet from `Process` was too long, or a packet it passed up with `Deliver` was too long |
| `sendError` | the link or the framing refused a packet with an error other than `busy` |
| `linkDown` | a packet queued when the connection went down under `VUB::Down::Fail`, or a byte of the receive ring that a down discarded |

A packet with no caller is one the stack makes itself: a bulk packet from a layer's `Next`, or a packet a layer sends from `Process`.
When such a packet is refused, it is counted. A packet the application passes to `Send` is different: `Send` returns the failure to the
caller (`messageTooLong`, or the status of the block that refused it), and nothing is counted. `Send` does not count a busy queue
either: it returns `busy` to the caller. A layer that gets `busy` from its outbox keeps the packet and sends it again. A busy refusal of a
packet with no caller is not counted either, under `layerError` or `transformError`, so a layer that waits for a busy block does not
raise the count.

The counters are 16-bit and stop at 65535.

## Size

A block or an option that is not in the chain has no RAM. The buffers are sized at compile time from the chain: the packet size is the
payload plus the layer headers and transform growths, and the framed size adds the framing's growth. Storage that a chain does not use
(the ring of a packet link, the bulk slot of a chain without bulk layers, the send buffers of a chain with no layer or transform) is an
empty type. The code that works on the buffers is plain inline functions on spans and indexes, shared by every stack in one image.

The defaults take most of the RAM of a small chain: the receive queue holds `ReceiveQueue` packets of the link's `MaxPacket` bytes, and
the ring holds `ReceiveRing` bytes. A bootloader sets `ReceiveQueue(1)` or a smaller ring. The costs of the configurations on the target
are in the table below.

### Measured configurations

Each row is one program built for a Cortex-M4 and a Cortex-M0+, minus its baseline. The costs are in bytes. Text is the flash of the
code and read-only data. RAM is the data and bss of the program. Every configuration has `MaxPayload(32)` and the default queues and
ring, and a link with `MaxPacket(64)`.

| Configuration | M4 text | M4 RAM | M0+ text | M0+ RAM |
|---|---:|---:|---:|---:|
| (a) packet link only | 676 | 616 | 956 | 616 |
| (b) stream link, COBS | 932 | 620 | 1212 | 620 |
| (c) (b) + CRC-16/MODBUS table | 1624 | 700 | 1912 | 700 |
| (d) (b) + CRC-16/MODBUS bitwise | 1144 | 700 | 1432 | 700 |
| (e) stream link, length prefix 1, CRC-8 bitwise | 1056 | 672 | 1348 | 672 |
| (f) stream link, gap framing, CRC-16 table | 2716 | 1720 | 2868 | 1720 |
| (g) (c) + VUB::Reliable (Retries 3, Window 4) | 4136 | 1116 | 4612 | 1116 |
| (h) (g) with the in-order option | 4436 | 1252 | 4952 | 1252 |
| (i) COBS, CRC-8, VUB::ReliableOf window 1, Retries 3 | 3236 | 912 | 3708 | 912 |
| breakdown of (i): the same without VUB::ReliableOf | 1096 | 692 | 1388 | 692 |
| CompactStreamingProtocol<20, 4> (bootloader use) | 2292 | 1116 | 2880 | 1116 |
| (j) stream link, COBS, CRC-16/MODBUS table, VUB::KeepAlive (default times) | 2604 | 932 | 2956 | 932 |
| (k) (g) + VUB::KeepAlive (default times) | 5184 | 1400 | 5680 | 1400 |

Measured with `arm-none-eabi-g++.exe (Arm GNU Toolchain 15.2.Rel1 (Build arm-15.86)) 15.2.1 20251203` on Windows 11, through Git Bash,
with `Common/Drivers/Interface/VUB/.tool/size/size.sh`.

The flags of each compile are `-std=c++20 -mcpu=cortex-m4 -mthumb -Os -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti
-Wall -Wextra -Werror`, with `-mcpu=cortex-m0plus` for the M0+. Each program is linked with `-Wl,--gc-sections --specs=nano.specs
--specs=nosys.specs`, and `arm-none-eabi-size --format=berkeley` reads it. The include paths are the script's `shim` folder (a
stand-in for the VHAL umbrella header), the script's folder, `Common` and `Periphery`.

Each configuration is one file that defines the stack and `SizeRun`, compiled with the shared `SizeMain.cpp` (which has `main`). Both
include the shared `SizeWire.h`, which has the link classes, the clock and the handler. `SizeRun` sends a 32-byte payload, processes,
feeds back what the link wrote (the bytes one by one on a stream link, the packet on a packet link), and processes again.

The reliable rows (g), (h), (i) and (k) call `SetSession` on their reliable layer with the boot value `0x1234` ahead of the first send, since
`Send` returns `noInit` until then. Their `SizeRun` calls `SizeRunReliable` instead of `SizeRunStream`. The first `Send` after the session
is set returns `busy` and starts the session, so the run sends again in each round until a `Send` queues the payload. The rounds take
turns with the wire, processing and then feeding back the bytes that are not fed, and the run ends in a round that puts no byte on the wire.
On the host, each of the four programs hands the whole 32-byte payload to the handler (528 bytes, the sum of its bytes).

The baseline makes the same calls with no VUB: `BaselineStream.cpp` writes the payload to a stream link and hands the bytes it took to
the handler, and `BaselinePacket.cpp` sends the payload on a packet link and hands the packet to the handler. Each baseline has the same
link class, clock, handler and fill of the payload as the configurations. Both baselines measure, for Cortex-M4, text 552 bytes and data
plus bss 180 bytes (data 8, bss 172), and for Cortex-M0+, text 568 bytes and data plus bss 180 bytes.

To run it, from the repository root:

```bash
bash Common/Drivers/Interface/VUB/.tool/size/size.sh
```

The script prints the table, then the toolchain and the baseline sizes. It needs `arm-none-eabi-g++` and `arm-none-eabi-size` on the
PATH, or `CXX` and `SIZE` set to other tools. Without the toolchain it says so and exits 0. It exits 1 when a program does not build. The
build output goes to a temporary folder outside the repository.

#### Against CompactStreamingProtocol

`CompactStreamingProtocol<20, 4>` is the bootloader's protocol (`Common/Utilities/Bootloader/Communication/BLECommunication.h`). Its row
is the bootloader's use of it: the four callbacks the bootloader passes (transmit, data received, stream complete and receive reset),
with bodies of the same kind, a packet link, and a 32-byte payload, against the packet baseline. The probe runs one protocol object as
both ends. `SizeRun` sends the payload, then feeds the wire back to the protocol and processes it, again and again, until no byte is left on
the wire that has not been fed. Every fragment and every acknowledgement goes through the wire, so the message completes and the handler runs. Its caveats:

- The protocol cuts a message into fragments of 16 bytes (a 20-byte packet less a 3-byte header and a CRC byte). The 32-byte payload is
  two fragments. VUB sends one packet of 32 bytes. Cutting large messages into fragments is the large-data part of VUB, which is not built, and the VUB
  rows here have none of it.
- The probe stubs the System port functions the protocol reaches: `GetMs` returns the probe tick, and `CriticalSection` and `Abort`
  are empty. The bootloader's own implementations are not counted.
- The protocol's callbacks are `std::function` members, and the row includes their code.
- Configuration (i) is over a byte-stream link with COBS, and the protocol's row is over a packet link, as on BLE. Both baselines have
  the same size, so the difference between the rows is the protocol and the stack, not the baseline.
- Configuration (i) uses the default ring of 256 bytes. The protocol's ring is 80 bytes. A bootloader that sets `ReceiveRing` smaller
  is not measured here.

Against the protocol, configuration (i) is 944 bytes larger in text on the M4 (3236 against 2292) and 828 bytes larger on the M0+
(3708 against 2880). Its RAM is 204 bytes smaller on the M4 (912 against 1116) and on the M0+ (912 against 1116). The design asks for
a stack with the protocol's properties to be no larger than the protocol on the same target, so configuration (i) does not meet it in text.
The breakdown row shows where the text goes: without `VUB::ReliableOf`, configuration (i) is 1096 bytes of text on the M4, and the layer
adds 2140 bytes. The layer carries what the protocol does not: sequence numbers with sessions and a sync, a window, a ring of owed
acknowledgements, retries with timeouts, and outcome reporting (see [VUB::Reliable](Layer/Reliable)). The measurement gives the size of
the layer as a whole; it does not split the layer's code further.

## Writing a block

A user block is a type that satisfies the concept of its kind. The example is a transform that appends the sum of the packet, one byte.
It is checked with the test kit's contract check ahead of its use in a chain:

```cpp
// A transform that appends one byte: the sum of the packet modulo 256. A packet whose sum does not match is refused.
struct SumCheck {
	static constexpr std::size_t growth = 1;
	static constexpr uint32 fingerprint = 0x53554D31;

	Result<std::size_t> Encode(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size() + growth) {
			return ResultStatus::invalidParameter;
		}

		uint8 sum = 0;
		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = in[index];
			sum = static_cast<uint8>(sum + in[index]);
		}

		out[in.size()] = sum;
		return in.size() + growth;
	}

	Result<std::size_t> Decode(const VUB::Meta&, std::span<uint8> packet) {
		if (packet.empty()) {
			return ResultStatus::invalidData;
		}

		std::size_t body = packet.size() - 1;
		uint8 sum = 0;
		for (std::size_t index = 0; index < body; index++) {
			sum = static_cast<uint8>(sum + packet[index]);
		}

		if (sum != packet[body]) {
			return ResultStatus::checksumError;
		}

		return body;
	}
};
```

The block is used like a built-in one. The link of the board is a packet link: it sends a whole packet, or reports `busy`.

```cpp
// The packet link of the board: it sends a whole packet, or reports busy when its transmitter has no room.
struct BoardLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex();

	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}
};

void OnPacket(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
}

using Board = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<SumCheck>()
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>();
}>;

BoardLink boardLink;
Board board(boardLink);

void BoardSend() {
	std::array<uint8, 3> hello = {1, 2, 3};
	board.Send(std::span<const uint8>(hello.data(), hello.size()));
	board.Process();
}
```

Run the block's contract check ahead of putting it into a chain (see the [test kit](TestKit/README)):

```cpp
bool CheckContract() {
	SumCheck sumCheck;
	bool passes = VUB::CheckTransform(sumCheck, 7, 500, 32);
	return passes;
}
```

A block that uses time declares `usesTime = true` and gets the clock from `Process`. A layer that makes bulk packets declares
`makesBulk = true` and implements `Next`; a layer that makes none does not implement `Next`.

The receive interrupt of a packet link calls `board.FeedPacket(packet)`; a byte-stream link calls `board.Feed(byte)` for each byte.

### Link state, Reset and OnLink

A link that reports its state declares `ReportsState()` in its traits, and keeps the sink the stack gives it in `SetStateSink`. The stack
gives the sink when it is built, and the link reports its current state through it at once. `Report` only records the state, so the
interrupt may call it. The stack gives back a null sink when it is destroyed.

```cpp
// A packet link that reports its state, as a USB device does. The stack gives its sink here; the interrupt reports the connection.
struct UsbLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex().ReportsState();

	// The stack gives its sink here, and the link reports its current state through it at once.
	void SetStateSink(VUB::StateSink value) {
		sink = value;
		sink.Report(configured);
	}

	// Called from the interrupt when the host configures or unconfigures the device. Report only records the state.
	void OnConfigured(bool value) {
		configured = value;
		sink.Report(value);
	}

	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}

	VUB::StateSink sink;
	bool configured = false;
};


using Usb = VUB::Stack<+[] {
	return VUB::Builder()
		.Link<UsbLink>()
		.MaxPayload(32)
		.WhileDown(VUB::Down::Fail)
		.OnReceive<&OnPacket>();
}>;
```

A framing that keeps a partial frame declares `Reset()`. The stack calls it on each change of the connection, so the first frame after a
change starts clean. This framing ends a frame at a newline byte, `0x0A`:

```cpp
// A framing that ends a frame at a newline byte, 0x0A. Growth is 1: the newline. A frame longer than 32 bytes is too long.
struct LineFraming {
	static constexpr bool usesTime = false;


	static constexpr std::size_t Growth(std::size_t length) {
		return length + 1;
	}


	Result<std::size_t> Frame(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (in.size() + 1 > out.size()) {
			return ResultStatus::messageTooLong;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = in[index];
		}

		out[in.size()] = 0x0A;
		return in.size() + 1;
	}


	VUB::FrameStatus Push(uint8 byte, std::span<uint8> out, std::size_t& length) {
		if (byte != 0x0A) {
			if (count < body.size()) {
				body[count] = byte;
			}

			count++;
			return VUB::FrameStatus::pending;
		}

		std::size_t frame = count;
		count = 0;
		if (frame == 0) {
			return VUB::FrameStatus::pending;
		}

		if (frame > body.size() || frame > out.size()) {
			return VUB::FrameStatus::tooLong;
		}

		for (std::size_t index = 0; index < frame; index++) {
			out[index] = body[index];
		}

		length = frame;
		return VUB::FrameStatus::packet;
	}


	// Drops the partial frame. The stack calls it when the link goes up or down, if the framing has it.
	void Reset() {
		count = 0;
	}


	std::array<uint8, 32> body{};
	std::size_t count = 0;
};
```

A layer with `OnLink(bool up, uint32 now)` is told of each change of the connection, in chain order, from `Process`. A layer that
declares `reportsPeer = true` may declare its peer down or up from its `Process`, with `outbox.SetPeer(up)`. The stack applies it after
the layers have run, as a change of the connection. This layer declares its peer down when nothing has been heard for 1000
microseconds:

```cpp
// A layer that declares its peer down when nothing has been heard from it for 1000 microseconds. It is told of each change of the connection.
struct PeerWatch {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x50575431;
	static constexpr bool usesTime = true;
	static constexpr bool makesBulk = false;
	static constexpr bool reportsPeer = true;


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size()) {
			return ResultStatus::invalidParameter;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = in[index];
		}

		return in.size();
	}


	Result<VUB::Taken> Take(const VUB::Meta&, std::span<const uint8>) {
		return VUB::Taken{0, true};
	}


	void Process(uint32 nowUs, const VUB::Outbox& outbox) {
		outbox.SetPeer(nowUs - heardUs < 1000);
	}


	// Called when a packet from the peer arrives.
	void Heard(uint32 nowUs) {
		heardUs = nowUs;
	}


	void OnLink(bool up, uint32 nowUs) {
		linkUp = up;
		changedUs = nowUs;
	}


	uint32 heardUs = 0;
	uint32 changedUs = 0;
	bool linkUp = false;
};


inline uint32 Now() {
	return 0;
}


using Watched = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<PeerWatch>()
		.Link<VUB::PacketEnd<64, 512>>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>()
		.Clock<&Now>();
}>;
```

### Passing a packet up from Process

A layer that holds packets, for in-order delivery, releases them from `Process` with `outbox.Deliver(packet)`. The packet is the one the
layer would pass up: its header removed, as its `Take` left it. It goes through the blocks above the layer, as a packet the layer had
just taken, and reaches the receive handler once. A block above that refuses it counts the drop as it counts a received packet.
`Deliver` is for `Process`; `Take` passes a packet up by returning it. A layer that calls `Deliver` declares `passesUp = true`. A chain
whose layers do not declare it has no code for it.

The packet is copied into the receive buffer, since the blocks above decode in place. It must fit the link's `MaxPacket`: a longer one
is refused with `messageTooLong` and counted under `layerError`. A layer that did not declare `passesUp` gets `notSupported` from
`Deliver`, and nothing goes up.

This layer holds one early packet. Each packet starts with a sequence byte, then the payload, so the packet with sequence 1 and payload
0x11 is `01 11` on the wire:

```cpp
// A layer that delivers in order. Each packet starts with a sequence number, one byte. A packet that comes early is held, and Process
// passes it up when the gap has filled. The layer holds one packet, for brevity.
struct InOrder {
	static constexpr std::size_t header = 1;
	static constexpr uint32 fingerprint = 0x494F5231;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;
	static constexpr bool passesUp = true;


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size() + header) {
			return ResultStatus::invalidParameter;
		}

		out[0] = sequence;
		sequence++;
		for (std::size_t index = 0; index < in.size(); index++) {
			out[header + index] = in[index];
		}

		return in.size() + header;
	}


	Result<VUB::Taken> Take(const VUB::Meta&, std::span<const uint8> packet) {
		if (packet.empty()) {
			return ResultStatus::invalidData;
		}

		if (packet[0] == expected) {
			expected++;
			return VUB::Taken{header, true};
		}

		if (held || packet.size() - header > heldBody.size()) {
			return ResultStatus::busy;
		}

		heldLength = packet.size() - header;
		for (std::size_t index = 0; index < heldLength; index++) {
			heldBody[index] = packet[header + index];
		}

		heldNumber = packet[0];
		held = true;
		return VUB::Taken{header, false};
	}


	void Process(uint32, const VUB::Outbox& outbox) {
		if (held && heldNumber == expected) {
			held = false;
			expected++;
			(void)outbox.Deliver(std::span<const uint8>(heldBody.data(), heldLength));
		}
	}


	uint8 sequence = 0;
	uint8 expected = 0;
	bool held = false;
	uint8 heldNumber = 0;
	std::size_t heldLength = 0;
	std::array<uint8, 8> heldBody{};
};


// The payloads the handler got, in order.
inline std::array<uint8, 4> delivered{};
inline std::size_t deliveredCount = 0;


void OnInOrder(const VUB::Meta&, std::span<const uint8> payload) {
	if (deliveredCount < delivered.size() && !payload.empty()) {
		delivered[deliveredCount] = payload[0];
	}

	deliveredCount++;
}


// A packet link. The interrupt gives it packets with FeedPacket.
struct InOrderLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex();

	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}
};


using InOrderStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<InOrder>()
		.Link<InOrderLink>()
		.MaxPayload(8)
		.ReceiveQueue(4)
		.OnReceive<&OnInOrder>();
}>;


void InOrderSample() {
	InOrderLink link;
	InOrderStack stack(link);
	std::array<uint8, 2> early = {0x01, 0x11};
	std::array<uint8, 2> first = {0x00, 0x10};
	stack.FeedPacket(early);
	stack.FeedPacket(first);
	stack.Process();
}
```

Packet 0 is taken and reaches the handler at once. Packet 1 was held, and `Process` releases it after packet 0, so the handler gets `0x10`,
then `0x11`. A second early packet, while one is held, is refused by `Take` with `busy`, which counts it under `layerError`. The release
of a held packet counts nothing; a block above that refuses it counts its own drop.

Measured: the text of a probe of the test kit's chains, none of which has a `passesUp` layer, is 26 bytes smaller than the same chains on a stack with the passesUp support. The probe was built with `arm-none-eabi-g++` 15.2, `-Os`, Cortex-M4, and sized with `arm-none-eabi-size`.

### Options

A block given options takes them as a value: `.Layer<T>(options)`, `.Transform<T>(options)` and `.Framing<T>(options)` pass the
value. A block that declares `Configured` takes them as template arguments, and any other block takes them in its constructor. The
two ways are below, after the example of the constructor.

A block can take its options flat, as methods right after it. It declares a member template `Step` that derives from the builder
it is given. The `Layer`, `Transform` and `Framing` calls of the block then return the Step, so the option methods follow the block
and the chain goes on with every builder method. Each option method reads the options with `LastOptions()`, changes one field and
returns the Step over `WithLastOptions(...)`. A block with a Step names its options `Options`; a block with `Configured` needs no
constructor for them, and any other block has a constructor that takes them. A call with no options gives it `Options{}`. The option methods exist only on the Step, so after any other block they do not
compile.

```cpp
// A transform that XORs each byte with a key. Its options give the key, and its Step gives the key as a method right after it.
struct XorKey {
	struct Options {
		uint8 key = 0;
	};


	template<typename Builder>
	struct Step : Builder {
		explicit constexpr Step(const Builder& base) : Builder(base) {
		}

		constexpr Step Key(uint8 value) const {
			Options options = this->LastOptions();
			options.key = value;
			return Step(this->WithLastOptions(options));
		}
	};

	static constexpr std::size_t growth = 0;
	static constexpr uint32 fingerprint = 0x584B4559;

	uint8 key = 0;

	XorKey() = default;

	explicit XorKey(const Options& options) : key(options.key) {
	}

	Result<std::size_t> Encode(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size()) {
			return ResultStatus::invalidParameter;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = static_cast<uint8>(in[index] ^ key);
		}

		return in.size();
	}

	Result<std::size_t> Decode(const VUB::Meta&, std::span<uint8> packet) {
		for (std::size_t index = 0; index < packet.size(); index++) {
			packet[index] = static_cast<uint8>(packet[index] ^ key);
		}

		return packet.size();
	}
};

using Board = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<XorKey>().Key(0x5A)
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>();
}>;
```

`Key(0x5A)` sets the key of the transform. A transform without `.Key(...)` gets key 0, and `.Transform<XorKey>(XorKey::Options{0x5A})`
gives the same key as a value.

#### Two ways to take options

The stack builds each block in one of two ways, and a block declares which one it takes:

- A block that declares `template<Options value> using Configured = ...;` takes its options as template arguments. The stack stores
  `Block::Configured<value>`, default-constructed, with the options of its chain entry. Every option is a constant of the stored block, so
  `if constexpr` removes the code of a feature that the value turns off, and the options take no RAM. The built-in blocks with options
  (`VUB::Reliable`, `VUB::KeepAlive` and `VUB::Gap`) take them this way. Use it when the chain fixes the options, which is the usual case
  for a block of a board's chain.
- A block without `Configured` is built from its options with its constructor, when the stack is built. The options are values, and the
  block keeps them at run time, so a feature that is off still has its code. Use it when the block needs a value that is not a constant
  (for example a pointer to an object of the program that is not known until run time), or when the block is kept by a program that does
  not give it to a stack. A user block without `Configured` keeps working as it did.

The options of a `Configured` block are a structural type: its members are integers, `bool` values and function pointers, public, with
default values. The option methods of the block (its Step) read and write the options the same way in both cases.

The example below is `XorKey` with the key as a template argument. Its option method is the same; the key is a constant of the stored
transform.

```cpp
// A transform that XORs each byte with a key given as a template argument. The stack stores it as Configured, so the key is a constant
// of the stored transform.
struct XorKeyOptions {
	uint8 key = 0;
};


template<XorKeyOptions Value = XorKeyOptions{}>
struct XorKeyOf {
	using Options = XorKeyOptions;

	template<Options value>
	using Configured = XorKeyOf<value>;

	template<typename Builder>
	struct Step : Builder {
		explicit constexpr Step(const Builder& base) : Builder(base) {
		}

		constexpr Step Key(uint8 value) const {
			Options options = this->LastOptions();
			options.key = value;
			return Step(this->WithLastOptions(options));
		}
	};

	static constexpr std::size_t growth = 0;
	static constexpr uint32 fingerprint = 0x584B4559;
	static constexpr uint8 key = Value.key;

	Result<std::size_t> Encode(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size()) {
			return ResultStatus::invalidParameter;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = static_cast<uint8>(in[index] ^ key);
		}

		return in.size();
	}

	Result<std::size_t> Decode(const VUB::Meta&, std::span<uint8> packet) {
		for (std::size_t index = 0; index < packet.size(); index++) {
			packet[index] = static_cast<uint8>(packet[index] ^ key);
		}

		return packet.size();
	}
};

using ConfiguredBoard = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<XorKeyOf<>>().Key(0x5A)
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>();
}>;
```

## Compilers

The host tests (`Common/Drivers/Interface/VUB/.tool/sim/run.sh`) pass with g++ 11.4 (Ubuntu 22.04, under WSL) and g++ 15.2 (MinGW, on
Windows 11). The header checks and the builder error snippets run with both. The microcontroller headers are checked alone with
`arm-none-eabi-g++` 15.2 (Arm GNU Toolchain 15.2.Rel1), and the size table is measured with it. Other compilers, clang++ among them, are
not tested.

## Status

Built: the core, the link state, the test kit, the host tests, the built-in blocks (CRC, COBS, length prefix, gap framing, delivery,
keep-alive), the size table, the links on a microcontroller and on a PC, and the board check on the STM32G431 over UART, USB CDC and
USB bulk.

Not built: requests and health, addressing, large data, encryption and the CRC engine, values and subscriptions, and the SPI links.
