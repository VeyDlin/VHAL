# VubLengthPrefix

Length-prefixed framing. Each packet is a header and the payload. The header says where the payload ends, and a check byte lets a
receiver that lost a byte find the next header.

Header: `Common/Drivers/Interface/VUB/Framing/VubLengthPrefix.h`. The width is a template argument: `VubLengthPrefix<1>` or
`VubLengthPrefix<2>`. A `Step` option cannot change it, because `Growth` is a constant of the type.

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

The packet `11 22 33 44 55` with `VubLengthPrefix<1>`, worked out by hand:

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
with `VubLengthPrefix<1>` and a packet bound over 255 is a compile error:
`a one byte length holds packets of 255 bytes at most: use VubLengthPrefix<2> for a longer packet`.

## Receiving and resynchronising

The receiver keeps a window of the last Width + 2 bytes while it looks for a header. When the window holds a whole header and its check
is good, the payload is as many bytes as the header's length says, and they follow the header.

- A window whose check fails drops its first byte, and the rest of the window is looked at again for a sync byte. So a receiver slides
  one byte at a time until it finds a good header.
- Bytes that are not a header are dropped without a count. They are noise, not packets, and the framing has no status for them.

## Lost bytes

The payload has no check, so the framing does not notice a lost payload byte. Its integrity is the job of a transform above it, a CRC.

The contract test for loss is `VubCheckResync`: a lossy part may give packets of wrong bytes, and the clean frames sent after it come out
exactly, except the first, which may merge with the damage. The test for no loss is `VubCheckFraming`, where every packet comes out
exactly.

- A lost byte in a header makes the header fail its check. The receiver slides past it and finds the next frame.
- A lost payload byte, on a line that cannot report its state: the payload is read for its full length, so the sync byte of the next
  frame is taken as its last byte, and a packet comes out with the wrong bytes. The next frame is lost, since its header is read as
  payload or dropped. The framing resynchronises at the frame after it.

Example: `A5 03 58 11 33 A5 03 58 AA BB CC A5 03 58 77 88 99`, with the `22` of the first frame lost. Packet `11 33 A5` comes out. The
bytes `03 58 AA BB CC` are dropped. Packet `77 88 99` comes out.

## Reset

`Reset()` drops the partial frame. The stack calls it when the link goes up or down.

## RAM

The window (Width + 2 bytes), the count of payload bytes, the expected length, and a flag: 20 bytes of object on a 32-bit target, beside
the receive buffer.

## Example

```cpp
struct Uart {
	static constexpr VubLinkTraits traits = VubLinkTraits().MaxPacket(64).FullDuplex();


	Result<std::size_t> Write(std::span<const uint8> bytes) {
		return bytes.size();
	}
};


void OnPacket(const VubMeta&, std::span<const uint8> payload) {
	(void)payload;
}


using Stack = Vub<+[] {
	return VubBuilder()
		.Framing<VubLengthPrefix<1>>()
		.Link<Uart>()
		.MaxPayload(64)
		.OnReceive<&OnPacket>();
}>;


Uart uart;
Stack stack(uart);


void Run() {
	const std::array<uint8, 3> hello = {0x11, 0x22, 0x33};
	stack.Send(std::span<const uint8>(hello.data(), hello.size()));
	stack.Process();
}
```

The example is compiled and run in `TestVubFramingWiki.cpp`. The contract and the resynchronisation are checked in
`TestVubLengthPrefix.cpp`.
