# VUB

VUB is a protocol stack built from blocks. A product's protocol is a chain of blocks, written in one builder and checked at compile
time. Each block has a small contract. The built-in blocks use only that contract, so a user block goes into the chain the same way.

Stage 1 is the core: the five block contracts, the builder and its checks, the stack at run time, drop counters, and a test kit. The
built-in blocks (CRC, COBS, length prefix, gap framing, delivery) come in the later stages. Until then a chain is made of user blocks
and the test kit's loopback.

## The chain

A chain is written from the application down to the link:

```cpp
using Board = Vub<+[] {
	return VubBuilder()
		.Transform<SumCheck>()
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>();
}>;
```

The application sends into the top, and the link is the last block. A block that is not in the chain has no code and no RAM.

## The five kinds

| Kind | Concept | What it does | Members the concept needs |
|---|---|---|---|
| Link | `VubStreamLink`, `VubPacketLink` | the wire at the bottom | `traits`; `Write(span)` returning `Result<size_t>` (a byte stream), or `Send(span)` returning `ResultStatus` (whole packets) |
| Framing | `VubFraming` | finds packet boundaries in a byte stream | `usesTime`, `Growth(length)`, `Frame(meta, packet, out)`, `Push(byte, out, length)` |
| Transform | `VubTransform` | one packet into another, and back | `growth`, `fingerprint`, `Encode(meta, in, out)`, `Decode(meta, packet)` |
| Engine | `VubEngine` | computes what a transform describes (a CRC, AES) | `synchronous` (the call shape comes with the first engine) |
| Layer | `VubLayer` | a header, state, time; may send packets of its own and make bulk packets | `header`, `fingerprint`, `usesTime`, `makesBulk`, `Add`, `Take`, `Process`, and `Next` when `makesBulk` is true |

A block that misses a member is a compile error. The message names the concept it does not satisfy, for example `the transform does not satisfy VubTransform`, and the concept's notes follow it.

## The builder

`VubBuilder()` makes an empty builder. Each method that adds a block returns a builder with one more block in its type. The four
block methods take the block's type, and optionally an options value:

- `.Layer<T>()` or `.Layer<T>(options)`
- `.Transform<T>()` or `.Transform<T>(options)`
- `.Framing<T>()` or `.Framing<T>(options)`
- `.Link<T>()` (the link takes no options; its object is built by the user)

The options value is passed to the block's constructor when the stack is built. A block given options must have a constructor that takes
them; otherwise the chain does not compile. A block given no options is default-constructed.

Settings are set anywhere in the chain:

| Method | Meaning | Default |
|---|---|---|
| `MaxPayload(bytes)` | the largest application payload | required |
| `ReceiveRing(bytes)` | bytes the receive ring holds (byte-stream links) | 256 |
| `ReceiveQueue(packets)` | packets the receive queue holds (packet links) | 4 |
| `SendQueue(packets)` | packets held for the link while it is busy | 4 |
| `PerProcess(packets)` | most packets one `Process` call sends | 8 |
| `Clock<&Fn>()` | the clock, a function returning microseconds as `uint32` | required when a block uses time |
| `OnReceive<&Fn>()` | the receive handler, `void(const VubMeta&, std::span<const uint8>)` | required |

The builder checks: one link, the link last; a byte-stream link has one framing right above it, a packet link has none; a link has
traits with `Framed()` only when it is a packet link, `HalfDuplex()` or `FullDuplex()`, and a non-zero `MaxPacket(n)`; the packet built
from the payload, the headers and the growths fits the link's `MaxPacket`; the sizes and the handler are given; a block that uses time
has a clock. The lengths and indexes are 16-bit: a packet, a framed packet and a link's `MaxPacket` are at most 65535 bytes, and the
ring and the queues hold at most 65535 entries. Each rule is a compile error with its message.

Options after a block: `LastIs<Block>()`, `LastOptions()` and `WithLastOptions(value)` read and replace the options of the last block.
A built-in block that has options after it (for example `.Reliable().Retries(3)`) is a method on the builder that checks `LastIs` and
calls `WithLastOptions`. A user block does not need this.

## Send and receive

`Send(payload, meta)` encodes at once: the payload goes through the layers and transforms, from the top down, and the result is put
in the control queue. `Process()` does the rest, in this order:

1. reads the clock (`Process` is the only place it is read);
2. takes the received bytes or packets, and decodes them from the bottom up: framing, transforms and layers (each layer removes its
   header), then the handler;
3. calls every layer's `Process(now, outbox)`, which may queue packets of its own;
4. sends.

`Feed(byte)` (byte-stream link) and `FeedPacket(packet)` (packet link) are called from the receive interrupt. They only store the data;
everything else runs in `Process`.

## Backpressure and order

A link may refuse data for a while: a USB class takes only part of a write, a transmitter is busy. The stack never drops an outgoing
packet for that reason.

- A byte-stream link's `Write` returns how many bytes it took. The stack keeps the rest of the framed packet and writes it on the next
  `Process`.
- A packet link's `Send` returns `ok` when it took the packet, and `busy` when it cannot take it now. The stack keeps the packet and
  tries again on the next `Process`. Any other status is an error: the packet is dropped and counted as `sendError`.
- `Process` keeps sending while the link accepts, up to `PerProcess` packets, so one call can move many packets. A packet that the
  framing or the link refuses with an error counts toward `PerProcess` as well, so a bulk layer that always has data cannot hold a call.
- `Send` and the layers' `Process` queue packets on the control lane, up to `SendQueue` packets. When the queue is full, `Send` returns
  `busy` and nothing is queued; the caller sends again later.

Order: control packets (from the application and from the layers' `Process`) go before bulk packets. A layer that makes bulk packets
(`makesBulk`) is asked for its next packet through `Next` only when the link is free and no control packet waits. The stack asks the
layers in chain order and takes the first one that has data. The packet from `Next` is complete at the layer's level: the layer added
its own header, and the blocks below add theirs.

## Flags

`VubMeta` has a `flags` field. The block that makes a packet sets it: the application through the `meta` argument of `Send`, a layer
in `Next`, a layer in `Process` through the outbox. Every block below the producer reads it through the `const VubMeta&` it is given:
the layers' `Add`, the transforms' `Encode` and `Decode`, and the framing's `Frame`. Links do not get it: a link carries bytes or
packets, not flags. `VubMeta::NoAcknowledgeFlag` is defined; the delivery layer that honours it is a later stage.

## Drop counters

Every packet or byte the stack discards is counted under one reason. `Drops()` returns a copy of the counters:

| Field | A packet or byte dropped because |
|---|---|
| `ringFull` | the receive interrupt found the ring full (byte-stream link) |
| `queueFull` | the receive interrupt found the queue full (packet link) |
| `tooLong` | a frame or packet did not fit in the receive buffer (the link's `MaxPacket`) |
| `framingError` | the bytes did not form a frame, or the frame's checks failed |
| `transformError` | a transform's `Decode` returned an error, or its `Encode` failed for a packet with no caller (see below) |
| `layerError` | a layer's `Take` returned an error, or a layer's `Add` or `Next` failed for a packet with no caller, or a layer's packet from `Process` was too long |
| `sendError` | the link or the framing refused a packet with an error other than `busy` |

A packet with no caller is one the stack makes itself: a bulk packet from a layer's `Next`, or a packet a layer sends from `Process`.
When such a packet is refused, it is counted. A packet the application passes to `Send` is different: `Send` returns the failure to the
caller (`messageTooLong`, or the status of the block that refused it), and nothing is counted. `Send` does not count a busy queue
either: it returns `busy` to the caller. A layer that gets `busy` from its outbox keeps the packet and sends it again.

The counters are 16-bit and stop at 65535.

## Size

A block or an option that is not in the chain has no RAM. The buffers are sized at compile time from the chain: the packet size is the
payload plus the layer headers and transform growths, and the framed size adds the framing's growth. Storage that a chain does not use
(the ring of a packet link, the bulk slot of a chain without bulk layers, the send buffers of a chain with no layer or transform) is an
empty type. The code that works on the buffers is plain inline functions on spans and indexes, shared by every stack in one image.

The defaults take most of the RAM of a small chain: the receive queue holds `ReceiveQueue` packets of the link's `MaxPacket` bytes, and
the ring holds `ReceiveRing` bytes. A bootloader sets `ReceiveQueue(1)` or a smaller ring. The size check of each configuration on the
target is a later stage.

## Writing a block

A user block is a type that satisfies the concept of its kind. The example is a transform that appends the sum of the packet, one byte.
It is checked with the test kit's contract check before it is used in a chain:

```cpp
// A transform that appends one byte: the sum of the packet modulo 256. A packet whose sum does not match is refused.
struct SumCheck {
	static constexpr std::size_t growth = 1;
	static constexpr uint32 fingerprint = 0x53554D31;

	Result<std::size_t> Encode(const VubMeta&, std::span<const uint8> in, std::span<uint8> out) {
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

	Result<std::size_t> Decode(const VubMeta&, std::span<uint8> packet) {
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
	static constexpr VubLinkTraits traits = VubLinkTraits().Framed().MaxPacket(64).FullDuplex();

	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}
};

void OnPacket(const VubMeta&, std::span<const uint8> payload) {
	(void)payload;
}

using Board = Vub<+[] {
	return VubBuilder()
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

Before the block goes into a chain, run its contract check (see the [test kit](TestKit/README)):

```cpp
SumCheck sumCheck;
bool passes = VubCheckTransform(sumCheck, 7, 500, 32);
```

A block that uses time declares `usesTime = true` and gets the clock from `Process`. A layer that makes bulk packets declares
`makesBulk = true` and implements `Next`; a layer that makes none does not implement `Next`.

The receive interrupt of a packet link calls `board.FeedPacket(packet)`; a byte-stream link calls `board.Feed(byte)` for each byte.

### Options

A block given options takes them as a value: `.Layer<T>(options)`, `.Transform<T>(options)` and `.Framing<T>(options)` pass the
value to the block's constructor.

A block can take its options flat, as methods right after it. It declares a member template `Step` that derives from the builder
it is given. The `Layer`, `Transform` and `Framing` calls of the block then return the Step, so the option methods follow the block
and the chain goes on with every builder method. Each option method reads the options with `LastOptions()`, changes one field and
returns the Step over `WithLastOptions(...)`. A block with a Step names its options `Options` and has a constructor that takes
them; a call with no options gives it `Options{}`. The option methods exist only on the Step, so after any other block they do not
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

	Result<std::size_t> Encode(const VubMeta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size()) {
			return ResultStatus::invalidParameter;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = static_cast<uint8>(in[index] ^ key);
		}

		return in.size();
	}

	Result<std::size_t> Decode(const VubMeta&, std::span<uint8> packet) {
		for (std::size_t index = 0; index < packet.size(); index++) {
			packet[index] = static_cast<uint8>(packet[index] ^ key);
		}

		return packet.size();
	}
};

using Board = Vub<+[] {
	return VubBuilder()
		.Transform<XorKey>().Key(0x5A)
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>();
}>;
```

`Key(0x5A)` sets the key of the transform. A transform without `.Key(...)` gets key 0, and `.Transform<XorKey>(XorKey::Options{0x5A})`
gives the same key as a value.

## Status

Stage 1a: the core, the test kit and the host tests. The built-in blocks (CRC, COBS, length prefix, gap framing, delivery, links for
the boards, addressing, requests) are later stages. Nothing is built for a board yet.
