# VUB::LengthPrefix

Length-prefixed framing. Each packet is a header and the payload. The header says where the payload ends, and a check byte lets a
receiver that lost a byte find the next header.

Header: `Common/Drivers/Interface/VUB/Framing/LengthPrefix.h`. The width is a template argument: `VUB::LengthPrefix<1>` or
`VUB::LengthPrefix<2>`. A `Step` option cannot change it, because `Growth` is a constant of the type.

## Wire format

| Byte | Value |
|---|---|
| 0 | the sync byte, `A5` |
| 1 to Width | the payload length, in bytes, little-endian |
| Width + 1 | the check: the byte that makes the sum of bytes 0 to Width come to `00` modulo 256 |
| after the header | the payload, as it is |

Examples, written by hand:

| Packet | Frame | Width |
|---|---|---|
| `11 22 33` | `A5 03 58 11 22 33` | 1 |
| `11 22 33` | `A5 03 00 58 11 22 33` | 2 |
| 256 bytes, `00` to `FF` | `A5 00 01 5A`, then the 256 bytes | 2 |

The check in the first line is `58` because `A5 + 03 + 58` is `100` in hexadecimal, which is 256.

## Worked example

The packet `11 22 33 44 55` with `VUB::LengthPrefix<1>`, worked out by hand:

1. The sync is `A5`.
2. The length is 5, so the length byte is `05`.
3. The sum of the sync and the length is `A5 + 05`, which is `AA`.
4. The check is the byte that makes the sum 00 modulo 256: `100 - AA` in hexadecimal is `56`.
5. The frame is `A5 05 56 11 22 33 44 55`. The receiver reads the header, and then the 5 bytes that the length says: `11 22 33 44 55` is one
   packet of 5 bytes.

A packet has at least one byte: `Frame` refuses an empty one. A header whose length is zero is not a header, even when its check is
good: `A5 00 5B` is refused, and the receiver slides past it.

## Growth

`Growth(n) = Width + 2` for any packet: the sync, the length and the check. A one-byte length holds packets of 255 bytes at most. A chain
with `VUB::LengthPrefix<1>` and a packet bound over 255 is a compile error:
`a one byte length holds packets of 255 bytes at most: use VUB::LengthPrefix<2> for a longer packet`.

## Receiving and resynchronising

The receiver keeps a window of the last Width + 2 bytes while it looks for a header. When the window holds a whole header and its check
is good, the payload is as many bytes as the header's length says, and they follow the header.

- A window whose check fails drops its first byte, and the rest of the window is looked at again for a sync byte. So a receiver slides
  one byte at a time until it finds a good header.
- Bytes that are not a header are dropped without a count. They are noise, not packets, and the framing has no status for them.

## Lost bytes

The payload has no check, so the framing does not notice a lost payload byte. Its integrity is the job of a transform above it, a CRC.

The contract test for loss is `VUB::CheckResync`: a lossy part may give packets of wrong bytes, and the clean frames sent after it come out
exactly, except the first, which may merge with the damage. The test for no loss is `VUB::CheckFraming`, where every packet comes out
exactly.

- A lost byte in a header makes the header fail its check. The receiver slides past it and finds the next frame.
- A lost payload byte, on a line that cannot report its state: the payload is read for its full length, so the sync byte of the next
  frame is taken as its last byte, and a packet comes out with the wrong bytes. The next frame is lost, since its header is read as
  payload or dropped. The framing resynchronises at the frame after it.

Example: `A5 03 58 11 33 A5 03 58 AA BB CC A5 03 58 77 88 99`, with the `22` of the first frame lost. Packet `11 33 A5` comes out. The
bytes `03 58 AA BB CC` are dropped. Packet `77 88 99` comes out.

## Refused headers and the cost of a false one

A header whose length is larger than the receive buffer is refused as a header. The framing learns the buffer from the `out` span it is
given with each byte. On a stream link that buffer is the packet bound of the largest payload the stack receives (`MaxReceive`, which is
`MaxPayload` by default), not the link's `MaxPacket`. The refused header's sync byte is dropped and the bytes after it are read again, so
the frame that follows comes out, and the refusal counts as too long. The framing does not know the chain's largest packet, only the
buffer, so a header between the buffer and the link's `MaxPacket` is refused as too long, as it should be on this chain. Passing the
chain's bound would need a value the framing does not have at run time, so this framing uses the buffer.

The cost of a false header, one whose check passes by chance (the check matches one sync byte in 256):

- A length larger than the buffer costs no bytes: the header is refused at once.
- A length the buffer holds swallows up to that many bytes, the payload it announces, and the framing then finds the next sync. A false
  header of 255 bytes swallows up to 255 bytes.

The framing needs a byte stream that keeps its order. The count of payload bytes is taken from the stream as it arrives, so under
reordered bytes a false header with a length the buffer holds can make the framing wait for the wrong bytes, and it may not find the next
sync again.

## Reset

`Reset()` drops the partial frame. The stack calls it when the link goes up or down.

## RAM

The window (Width + 2 bytes), the count of payload bytes, the expected length, and a flag: 20 bytes of object on a 32-bit target, beside
the receive buffer.

## Example

```cpp
struct Uart {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().MaxPacket(64).FullDuplex();


	Result<std::size_t> Write(std::span<const uint8> bytes) {
		return bytes.size();
	}
};


void OnPacket(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
}


using Device = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::LengthPrefix<1>>()
		.Link<Uart>()
		.MaxPayload(64)
		.OnReceive<&OnPacket>();
}>;


Uart uart;
Device device(uart);


void Run() {
	const std::array<uint8, 3> hello = {0x11, 0x22, 0x33};
	device.Send(std::span<const uint8>(hello.data(), hello.size()));
	device.Process();
}
```

The example is compiled and run in `TestVubFramingWiki.cpp`. The contract and the resynchronisation are checked in
`TestVubLengthPrefix.cpp`.
