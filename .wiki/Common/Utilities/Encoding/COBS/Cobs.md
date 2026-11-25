# Cobs

Consistent Overhead Byte Stuffing (COBS) by Cheshire and Baker. The codec turns a packet into a frame that has no zero byte except the last one, so a zero on a serial line marks the end of a frame. It keeps no buffer: the caller provides every span.

Header: `Common/Utilities/Encoding/COBS/Cobs.h`, namespace `Encoding`, class `Cobs`.

## Table of Contents

- [Quick Start](#quick-start)
- [Wire Format](#wire-format)
- [API Reference](#api-reference)
  - [EncodedSize](#encodedsize)
  - [Encode](#encode)
  - [Decode](#decode)
  - [Decoder](#decoder)
- [Errors](#errors)
- [Constant Expressions](#constant-expressions)
- [Tests](#tests)

## Quick Start

```cpp
#include <Utilities/Encoding/COBS/Cobs.h>

using Encoding::Cobs;

std::array<uint8, 4> packet = { 0x11, 0x22, 0x00, 0x33 };
std::array<uint8, Cobs::EncodedSize(4)> frame {};

// Encode: the frame is frame[0 .. length), its last byte is the zero delimiter
Result<std::size_t> encoded = Cobs::Encode(std::span<const uint8>(packet), std::span<uint8>(frame));
if (encoded.IsOk()) {
	// Send frame[0 .. encoded.Value())
}

// Decode in place: the packet is written over the frame
Result<std::size_t> decoded = Cobs::Decode(std::span<uint8>(frame).first(encoded.Value()));
```

## Wire Format

A frame is a list of blocks followed by one zero byte, the delimiter. Each block starts with a code byte n, from 01 to FF:

- n - 1 data bytes follow. They are never zero.
- If n is less than FF, a zero byte belongs to the packet after them. It is not sent: the code of the next block takes its place.
- If n is FF, 254 data bytes follow and no zero belongs to the packet after them.
- The last block of a packet is always written, even when it is empty, except that a packet ending with a block of 254 bytes has no code after that block.

The packet `00` is the frame `01 01 00`. The packet `00 11 22 33` is the frame `01 04 11 22 33 00`. The packet `11 22 00 33` is the frame `03 11 22 02 33 00`. The empty packet is the frame `01 00`.

A frame has no zero byte except its delimiter, so a receiver that sees a zero byte knows where a frame ends, whatever bytes came earlier.

## API Reference

All members are `static` except the decoder object. The class has no state.

### EncodedSize

```cpp
static constexpr std::size_t EncodedSize(std::size_t length);
```

Returns the most bytes a frame of `length` packet bytes takes, delimiter included: `length + length / 254 + 2`. Without the delimiter, subtract one. Use it to size the output span, as `std::array<uint8, Cobs::EncodedSize(4)>` does above.

### Encode

```cpp
static Result<std::size_t> Encode(std::span<const uint8> in, std::span<uint8> out);
```

Encodes `in` into `out` and returns the frame length, delimiter included. The check against `EncodedSize(in.size())` runs first, so a failed call leaves `out` unchanged. An empty `in` is a valid packet and gives the frame `01 00`.

```cpp
static constexpr std::size_t EncodeUnchecked(std::span<const uint8> in, std::span<uint8> out);
```

The same without the check, for a caller that has checked that `out` holds `EncodedSize(in.size())` bytes. It returns the frame length.

### Decode

```cpp
static Result<std::size_t> Decode(std::span<const uint8> in, std::span<uint8> out);
static Result<std::size_t> Decode(std::span<uint8> frame);
```

The first overload decodes the frame `in` into `out` and returns the packet length. The zero delimiter must be the last byte of `in`; the frame is the whole input.

The second overload decodes in place. The packet is written from the start of `frame`, and its length is returned. The packet never catches up with the bytes still to be read, so the in-place form is safe.

### Decoder

```cpp
class Decoder {
public:
	constexpr Status Push(uint8 byte, std::span<uint8> out, std::size_t& length);
	constexpr void Reset();
};
```

The decoder takes one byte at a time and writes the packet into the caller's span as the bytes arrive. It needs no buffer of its own. `Push` returns a `Status`:

| Status | Meaning |
|---|---|
| `pending` | The frame goes on. Also returned for a zero that comes with no block open, which is nothing between frames. |
| `packet` | A complete frame. `length` is set to the packet length. |
| `broken` | The frame ended inside a block, so it is dropped. |
| `tooLong` | The frame is complete, but `out` was too small for the packet. |

A zero byte ends the frame. The next byte starts the following frame, so a broken or overlong frame does not lose the frame after it. `Reset` drops a partial frame. The state is a code, a count of bytes the block still owes, a pending zero, an overflow flag and a count of written bytes.

```cpp
Cobs::Decoder decoder;
std::array<uint8, 32> packet {};
std::size_t length = 0;

for (uint8 byte : received) {
	if (decoder.Push(byte, std::span<uint8>(packet), length) == Cobs::Status::packet) {
		// packet[0 .. length) is a complete packet
	}
}
```

## Errors

`Encode` and `Decode` return `Result<std::size_t>`. Test `IsOk()` and then read `Value()`; `Error()` gives the `ResultStatus`.

| Function | Error | Condition |
|---|---|---|
| `Encode` | `messageTooLong` | `out` is smaller than `EncodedSize(in.size())`. |
| `Decode` | `invalidArgument` | `in` is empty. |
| `Decode` | `dataCorrupted` | The frame has no delimiter, a zero comes before the last byte, or a block runs into the delimiter. |
| `Decode` | `bufferOverflow` | The packet is longer than `out`. |

COBS has no check of its own. A damaged frame can decode to a different packet of the same length. The integrity of the payload needs a check above the codec.

## Constant Expressions

`EncodedSize`, `EncodeUnchecked`, `Decoder::Push` and `Decoder::Reset` are `constexpr`. `Encode` and `Decode` are not: they return `Result`, which is not a literal type.

```cpp
static_assert(Cobs::EncodedSize(254) == 257);
```

## Tests

The tests are in `Common/Utilities/Encoding/COBS/.tool/sim/`. `run.sh` builds each `Test*.cpp` with g++ at C++20, `-Wall -Wextra -Werror`, runs it, and checks that the header compiles alone.

- `TestCobsVectors.cpp`: the vectors of the paper and of the Wikipedia article, written by hand, including the blocks of 253, 254 and 255 bytes. Every packet of 0 to 3 bytes over `00`, `01` and `FF`. Random packets of 0 to 1000 bytes, checked against a reference encoder written from the block rules. Round trips with in-place and two-span decoding. `EncodedSize` bounds. Errors.
- `TestCobsStream.cpp`: the decoder one byte at a time, garbage and resynchronisation, overflow, `Reset`, and constant-expression use.
