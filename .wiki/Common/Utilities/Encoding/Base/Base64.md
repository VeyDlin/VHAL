# Base64

Header-only Base64 encoding and decoding library for embedded and general C++ projects.

## Table of Contents

- [Quick Start](#quick-start)
- [API Reference](#api-reference)
  - [GetEncodedSize](#getencodedsize)
  - [GetDecodedSize](#getdecodedsize)
  - [Encode (binary)](#encode-binary-data)
  - [Encode (string)](#encode-string)
  - [Decode](#decode)
  - [DecodeToBuffer](#decodetobuffer)
  - [DecodeToArray](#decodetoarray)
- [Details](#details)

## Quick Start

```cpp
#include <Utilities/Encoding/Base/Base64.h>

// Encode a string
std::array<char, 64> encoded{};
size_t len = Base64::Encode(std::span(encoded), "Hello World");
// encoded now contains "SGVsbG8gV29ybGQ="

// Decode back
std::array<uint8, 64> decoded{};
size_t decodedLen;
bool ok = Base64::Decode("SGVsbG8gV29ybGQ=", std::span(decoded), decodedLen);
```

## API Reference

All methods are `static`. The class has no state -- just call `Base64::MethodName(...)`.

### GetEncodedSize

```cpp
static size_t GetEncodedSize(size_t inputSize);
```

Returns the number of characters required to Base64-encode `inputSize` bytes (always a multiple of 4).

```cpp
size_t needed = Base64::GetEncodedSize(10); // 16
```

### GetDecodedSize

```cpp
static size_t GetDecodedSize(size_t encodedSize);
```

Returns the maximum number of bytes that a Base64 string of `encodedSize` characters can decode to. This is an upper bound -- the actual size may be smaller if padding is present.

```cpp
size_t maxBytes = Base64::GetDecodedSize(16); // 12
```

### Encode (binary data)

```cpp
static size_t Encode(std::span<char> output, std::span<const uint8> data);
```

Encodes raw binary data into Base64 text. Returns the number of characters written, or `0` if the output buffer is too small or the input is empty.

```cpp
std::array<uint8, 4> data = {0xDE, 0xAD, 0xBE, 0xEF};
std::array<char, 8> out{};
size_t len = Base64::Encode(std::span(out), std::span(data));
// out contains "3q2+7w==", len == 8
```

### Encode (string)

```cpp
static size_t Encode(std::span<char> output, std::string_view str);
```

Convenience overload that encodes a string (treated as raw bytes).

```cpp
std::array<char, 100> out{};
size_t len = Base64::Encode(std::span(out), "{\"alg\":\"HS256\"}");
```

### Decode

```cpp
static bool Decode(std::string_view encoded, std::span<uint8> output, size_t& outputSize);
```

Decodes a Base64 string into a binary buffer. Whitespace in the input is silently skipped. Returns `true` on success. On failure (invalid characters, bad length, buffer too small) returns `false` and sets `outputSize` to 0.

```cpp
std::array<uint8, 64> buf{};
size_t len;
if (Base64::Decode("SGVsbG8=", std::span(buf), len)) {
    // buf[0..len) contains {0x48, 0x65, 0x6C, 0x6C, 0x6F} == "Hello"
}
```

**Note:** The internal clean-input buffer is 512 bytes, so encoded strings longer than 512 non-whitespace characters will be silently truncated.

### DecodeToBuffer

```cpp
static bool DecodeToBuffer(std::string_view encoded, std::span<char> output, size_t& outputSize);
```

Decodes Base64 into a `char` buffer instead of `uint8`. Useful when the encoded payload is known to be text. Uses an internal 1024-byte intermediate buffer.

```cpp
std::array<char, 100> text{};
size_t textLen;
if (Base64::DecodeToBuffer("SGVsbG8gV29ybGQ=", std::span(text), textLen)) {
    // text[0..textLen) == "Hello World"
}
```

### DecodeToArray

```cpp
template<size_t MaxSize = 1024>
static bool DecodeToArray(std::string_view encoded, std::array<uint8, MaxSize>& output, size_t& outputSize);
```

Template convenience wrapper around `Decode` that accepts a fixed-size `std::array` directly.

```cpp
std::array<uint8, 256> arr{};
size_t len;
if (Base64::DecodeToArray(encodedStr, arr, len)) {
    // use arr[0..len)
}
```

## Details

- **Alphabet:** Standard Base64 (`A-Z`, `a-z`, `0-9`, `+`, `/`) with `=` padding.
- **Header-only:** No `.cpp` file needed; include `Base64.h` and use directly.
- **No dynamic allocation:** All buffers are caller-provided spans or fixed-size arrays -- suitable for embedded/bare-metal use.
- **Decode tables** are initialized once via a static IIFE (immediately-invoked lambda). The encode table is `constexpr`.
