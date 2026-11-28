# COBS

Consistent Overhead Byte Stuffing (COBS) encoder/decoder with configurable framing and escape bytes, designed for embedded serial communication.

This is not standard COBS. A frame starts with a start byte (0xFF by default) and ends with a stop byte (0x00 by default). The payload is
escaped first: the start, stop and escape bytes become the escape byte followed by a code (by default 0xFE then 0x01, 0x02 or 0x03).
The escaped bytes are then split into blocks of at most 254 bytes, each block preceded by a code byte that counts the block's bytes plus
one (0xFF for a full block). With the default configuration the escaped bytes hold no zero, so a block ends only when it is full or the payload ends. A zero
in the escaped bytes, which a stop byte other than zero allows, ends a block as in standard COBS.

The standard format is `Encoding/COBS` (`Common/Utilities/Encoding/COBS/Cobs.h`): it has no start byte and no escaping, and its frame
has a zero byte only as the delimiter. Use `Encoding/COBS` for a link that ends each frame with a zero byte. Use this class where the
start byte and the escape codes are the agreed format of the link, and the framing is configured through `Config`.

## Table of Contents

- [Quick Start](#quick-start)
- [Config](#config)
- [API Reference](#api-reference)
  - [Constructor](#constructor)
  - [GetConfig](#getconfig)
  - [Encode](#encode)
  - [Decode](#decode)
- [Error Handling](#error-handling)
- [Template Parameter](#template-parameter)

## Quick Start

```cpp
#include <Utilities/Serialization/COBS/COBS.h>

// Create encoder/decoder with default config
COBS<128> cobs({ });

uint8 input[] = { 0x10, 0x20, 0x00, 0x30 };
uint8 encoded[128];
uint8 decoded[128];

// Encode
Result<size_t> encResult = cobs.Encode(input, sizeof(input), encoded, sizeof(encoded));
if (encResult.IsOk()) {
    size_t encodedLength = encResult.Value();

    // Decode
    Result<size_t> decResult = cobs.Decode(encoded, encodedLength, decoded, sizeof(decoded));
    if (decResult.IsOk()) {
        size_t decodedLength = decResult.Value();
        // decoded now matches input
    }
}
```

## Config

The `COBS::Config` struct defines the framing and escape byte values:

```cpp
struct Config {
    uint8 startByte    = 0xFF;  // Frame start marker
    uint8 stopByte     = 0x00;  // Frame end marker
    uint8 escapeByte   = 0xFE;  // Escape prefix
    uint8 escapedStart = 0x01;  // Escape code for startByte
    uint8 escapedStop  = 0x02;  // Escape code for stopByte
    uint8 escapedEscape = 0x03; // Escape code for escapeByte
};
```

All three marker bytes (`startByte`, `stopByte`, `escapeByte`) must be distinct; the constructor asserts this at runtime.

### Custom Configuration Example

```cpp
COBS<256> cobs({
    .startByte    = 0xAA,
    .stopByte     = 0x55,
    .escapeByte   = 0xBB,
    .escapedStart = 0x01,
    .escapedStop  = 0x02,
    .escapedEscape = 0x03
});
```

## API Reference

### Constructor

```cpp
COBS(const Config& config);
```

Creates a COBS instance with the given configuration. Asserts that `startByte`, `stopByte`, and `escapeByte` are all different values.

### GetConfig

```cpp
Config GetConfig() const;
```

Returns a copy of the current configuration.

```cpp
COBS<128> cobs({ });

COBS<128>::Config config = cobs.GetConfig(); // config.startByte is 0xFF
```

### Encode

```cpp
Result<size_t> Encode(const uint8* data, size_t length, uint8* output, size_t maxOutputLength) const;
```

Encodes raw data into a COBS-framed packet.

**Parameters:**
| Parameter | Description |
|---|---|
| `data` | Pointer to the input data |
| `length` | Number of bytes to encode |
| `output` | Pointer to the output buffer |
| `maxOutputLength` | Size of the output buffer |

**Returns:** `Result<size_t>` containing the number of bytes written to `output`, or an error status.

**Output format:** `[startByte] [COBS-encoded escaped payload] [stopByte]`

**Output buffer size:** `Encode` checks the size before it writes anything. `maxOutputLength` must be at least `escapedLength + escapedLength / 254 + 3`, where `escapedLength` is the payload length after escaping. The `+ 3` covers the start byte, the stop byte and the first block code. Each block holds at most 254 data bytes, so one extra block code is needed for every 254 escaped bytes. A run of 255 or more non-zero bytes is split into several blocks.

Worst case, every input byte escapes to two bytes, so a buffer of `2 * length + (2 * length) / 254 + 3` bytes always fits `length` input bytes.

```cpp
COBS<128> cobs({ });

uint8 payload[] = { 0x01, 0x02, 0x03 };
uint8 frame[128];

Result<size_t> result = cobs.Encode(payload, 3, frame, sizeof(frame));
if (result.IsOk()) {
    // frame[0]              == 0xFF (startByte)
    // frame[result.Value() - 1] == 0x00 (stopByte)
    // Send frame, result.Value() bytes total
}
```

### Decode

```cpp
Result<size_t> Decode(const uint8* data, size_t length, uint8* output, size_t maxOutputLength) const;
```

Decodes a COBS-framed packet back into raw data.

**Parameters:**
| Parameter | Description |
|---|---|
| `data` | Pointer to the framed packet (must start with `startByte` and end with `stopByte`) |
| `length` | Total length of the framed packet |
| `output` | Pointer to the output buffer for decoded data |
| `maxOutputLength` | Size of the output buffer |

**Returns:** `Result<size_t>` containing the number of decoded bytes, or an error status.

```cpp
COBS<128> cobs({ });

// Assume 'frame' and 'frameLen' come from Encode or a serial port
uint8 decoded[128];
Result<size_t> result = cobs.Decode(frame, frameLen, decoded, sizeof(decoded));
if (result.IsOk()) {
    // Use result.Value() decoded bytes
}
```

## Error Handling

`Encode` and `Decode` return `Result<size_t>`. Check `IsOk()` before accessing the byte count with `Value()`; use `Error()` to retrieve the `ResultStatus` when the operation fails.

| Error | Condition |
|---|---|
| `ResultStatus::bufferOverflow` | Input or escaped payload exceeds `TempBufferSize`, or output buffer is too small |
| `ResultStatus::invalidArgument` | Frame is shorter than 2 bytes, or missing start/stop markers |
| `ResultStatus::dataCorrupted` | A block code is `0`, a block runs past the stop byte (truncated frame), or an escape sequence is malformed |

## Template Parameter

```cpp
template <size_t TempBufferSize = 256>
class COBS;
```

`TempBufferSize` sets the size of a stack-allocated intermediate buffer used during encoding and decoding. It must be at least as large as the escaped payload. The default is 256 bytes. Increase it if you need to handle larger packets:

```cpp
COBS<1024> cobs({ }); // Intermediate capacity: up to 1024 bytes after escaping
```
