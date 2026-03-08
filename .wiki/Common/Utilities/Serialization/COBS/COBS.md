# COBS

Consistent Overhead Byte Stuffing (COBS) encoder/decoder with configurable framing and escape bytes, designed for embedded serial communication.

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
auto encResult = cobs.Encode(input, sizeof(input), encoded, sizeof(encoded));
if (!encResult.IsError()) {
    size_t encodedLength = encResult.data;

    // Decode
    auto decResult = cobs.Decode(encoded, encodedLength, decoded, sizeof(decoded));
    if (!decResult.IsError()) {
        size_t decodedLength = decResult.data;
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

### Encode

```cpp
Status::info<size_t> Encode(const uint8* data, size_t length, uint8* output, size_t maxOutputLength) const;
```

Encodes raw data into a COBS-framed packet.

**Parameters:**
| Parameter | Description |
|---|---|
| `data` | Pointer to the input data |
| `length` | Number of bytes to encode |
| `output` | Pointer to the output buffer |
| `maxOutputLength` | Size of the output buffer |

**Returns:** `Status::info<size_t>` containing the number of bytes written to `output`, or an error status.

**Output format:** `[startByte] [COBS-encoded escaped payload] [stopByte]`

```cpp
COBS<128> cobs({ });

uint8 payload[] = { 0x01, 0x02, 0x03 };
uint8 frame[128];

auto result = cobs.Encode(payload, 3, frame, sizeof(frame));
if (!result.IsError()) {
    // frame[0]              == 0xFF (startByte)
    // frame[result.data - 1] == 0x00 (stopByte)
    // Send frame, result.data bytes total
}
```

### Decode

```cpp
Status::info<size_t> Decode(const uint8* data, size_t length, uint8* output, size_t maxOutputLength) const;
```

Decodes a COBS-framed packet back into raw data.

**Parameters:**
| Parameter | Description |
|---|---|
| `data` | Pointer to the framed packet (must start with `startByte` and end with `stopByte`) |
| `length` | Total length of the framed packet |
| `output` | Pointer to the output buffer for decoded data |
| `maxOutputLength` | Size of the output buffer |

**Returns:** `Status::info<size_t>` containing the number of decoded bytes, or an error status.

```cpp
COBS<128> cobs({ });

// Assume 'frame' and 'frameLen' come from Encode or a serial port
uint8 decoded[128];
auto result = cobs.Decode(frame, frameLen, decoded, sizeof(decoded));
if (!result.IsError()) {
    // Use decoded, result.data bytes
}
```

## Error Handling

All public methods return `Status::info<size_t>`. Check for errors with `IsError()` before accessing `.data`.

| Error | Condition |
|---|---|
| `Status::bufferOverflow` | Input exceeds `TempBufferSize`, or output buffer is too small |
| `Status::invalidArgument` | Frame is shorter than 2 bytes, or missing start/stop markers |
| `Status::dataCorrupted` | COBS block structure is invalid, or escape sequence is malformed |

## Template Parameter

```cpp
template <size_t TempBufferSize = 256>
class COBS;
```

`TempBufferSize` sets the size of a stack-allocated intermediate buffer used during encoding and decoding. It must be at least as large as the escaped payload. The default is 256 bytes. Increase it if you need to handle larger packets:

```cpp
COBS<1024> cobs({ }); // Supports payloads up to 1024 bytes (before escaping)
```
