# VubCobs

COBS framing as Cheshire and Baker describe it. A frame is the packet with its zero bytes taken out, and it ends with a zero byte.
Header: `Common/Drivers/Interface/VUB/Framing/VubCobs.h`. It is not the scheme of `Serialization/COBS/COBS.h`, which is a different one.

## Wire format

A frame is a list of blocks, then the zero byte 00, which is not part of the packet. Each block starts with a code byte n, from 01 to
FF:

- n - 1 data bytes follow. The data bytes are never zero.
- If n is less than FF, a zero byte belongs to the packet after them. It is not sent: the code of the next block takes its place.
- If n is FF, 254 data bytes follow and no zero.

The last block of a frame has no zero after it. Examples, written by hand:

| Packet | Frame |
|---|---|
| `00` | `01 01 00` |
| `00 00` | `01 01 01 00` |
| `11 22 00 33` | `03 11 22 02 33 00` |
| `11 00 00 00` | `02 11 01 01 01 00` |
| 254 bytes, all non-zero | `FF`, the 254 bytes, `00` (256 bytes) |
| 255 bytes, all non-zero | `FF`, the first 254 bytes, `02`, the 255th byte, `00` (258 bytes) |

`Frame` refuses an empty packet. A frame with no data, `01 00`, is broken on receipt.

## Growth

`Growth(n) = n + n / 254 + 2`: one code byte at least, one more for each 254 bytes, and the delimiter. The bound is exact for a packet
of 255 bytes, and one more than needed for 254 bytes (256 bytes sent, 257 reserved).

With the leading delimiter, `VubCobsFlushed`, the bound is one more: `Growth(n) = n + n / 254 + 3`.

## Leading delimiter

`VubCobsFlushed` sends a zero before each frame too. The zero ends whatever noise came before it on the line.

It is a separate type and not a `Step` option. `Growth` is a constant of the type, and the stack sizes its buffers from it, so an option
that changes it would cost buffer space in every chain, whether the option is on or off.

## Receiving

Bytes are decoded one at a time, and the packet is written into the output as its bytes come in.

- A zero byte ends the frame. The frame is broken if its last block is still open at that zero (counted as a framing error).
- A packet longer than the output is `tooLong` at its zero.
- After garbage, the next zero byte starts the next frame.

COBS has no check. A damaged frame can come out as a different packet of the same bytes. The integrity of the payload is the job of a
transform above the framing, such as a CRC.

The contract test for loss is `VubCheckResync`: a lossy part may give packets of wrong bytes, and the clean frames sent after it come out
exactly, except the first, which may merge with the damage. The test for no loss is `VubCheckFraming`, where every packet comes out
exactly.

## Reset

`Reset()` drops the partial frame. The stack calls it when the link goes up or down.

## RAM

A count of the bytes written, the code of the open block, the bytes it still owes, and two flags: 8 bytes of object on a 32-bit target,
beside the receive buffer.

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
		.Framing<VubCobs>()
		.Link<Uart>()
		.MaxPayload(64)
		.OnReceive<&OnPacket>();
}>;


Uart uart;
Stack stack(uart);


void Run() {
	const std::array<uint8, 3> hello = {0x11, 0x00, 0x22};
	stack.Send(std::span<const uint8>(hello.data(), hello.size()));
	stack.Process();
}
```

The example is compiled and run in `TestVubFramingWiki.cpp`. The contract is checked in `TestVubCobs.cpp`.
