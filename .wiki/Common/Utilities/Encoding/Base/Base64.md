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
// encoded[0..len) now contains "SGVsbG8gV29ybGQ="

// Decode back
std::array<uint8, 64> decoded{};
size_t decodedLen = 0;
bool ok = Base64::Decode("SGVsbG8gV29ybGQ=", std::span(decoded), decodedLen);
// ok = true, decoded[0..decodedLen) == "Hello World"
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

Encodes raw binary data into standard Base64 text with `=` padding. Returns the number of characters written.

- Returns `0` if `data` is empty.
- Returns `0`, and writes nothing, if `output` is smaller than `GetEncodedSize(data.size())`.
- Does **not** write a NUL terminator. Use the returned length, or add one yourself if the consumer needs a C string.

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

Convenience overload that encodes a string (treated as raw bytes). Same return value and rules as the binary overload.

```cpp
std::array<char, 100> out{};
size_t len = Base64::Encode(std::span(out), "{\"alg\":\"HS256\"}");
```

### Decode

```cpp
static bool Decode(std::string_view encoded, std::span<uint8> output, size_t& outputSize);
```

Decodes standard Base64 into a binary buffer. The input may be any length; only the output span limits the result.

- ASCII whitespace (space, tab, CR, LF) anywhere in the input is skipped.
- Returns `false` when the input contains a character outside the Base64 alphabet (for example `-`, `_` or a non-ASCII byte), when the number of non-whitespace characters is not a multiple of 4, when padding (`=`) is misplaced or more than two characters long, or when `output` is smaller than the decoded size.
- On failure `outputSize` is `0` and `output` is not written. A failing call never returns a partial result.
- Trailing bits of the final group are not checked: `Zh==` decodes to `f` the same as `Zg==`.
- Empty input (or only whitespace) succeeds with `outputSize == 0`.

```cpp
std::array<uint8, 64> buf{};
size_t len = 0;
if (Base64::Decode("SGVsbG8=", std::span(buf), len)) {
    // buf[0..len) contains {0x48, 0x65, 0x6C, 0x6C, 0x6F} == "Hello"
}
```

### DecodeToBuffer

```cpp
static bool DecodeToBuffer(std::string_view encoded, std::span<char> output, size_t& outputSize);
```

Decodes Base64 into a `char` buffer instead of `uint8`. Useful when the encoded payload is known to be text. Same rules as `Decode`: fails, with `outputSize` 0, when the decoded data does not fit into `output`. The bytes are written directly into `output`; there is no intermediate buffer.

```cpp
std::array<char, 100> text{};
size_t textLen = 0;
if (Base64::DecodeToBuffer("SGVsbG8gV29ybGQ=", std::span(text), textLen)) {
    // text[0..textLen) == "Hello World"
}
```

### DecodeToArray

```cpp
template<size_t MaxSize = 1024>
static bool DecodeToArray(std::string_view encoded, std::array<uint8, MaxSize>& output, size_t& outputSize);
```

Template convenience wrapper around `Decode` that accepts a fixed-size `std::array` directly. The same failure rules apply.

```cpp
std::array<uint8, 256> arr{};
size_t len = 0;
if (Base64::DecodeToArray(encodedStr, arr, len)) {
    // use arr[0..len)
}
```

## Details

- **Alphabet:** Standard Base64 (`A-Z`, `a-z`, `0-9`, `+`, `/`) with `=` padding. The URL-safe alphabet (`-`, `_`) is not accepted; translate it first.
- **Header-only:** No `.cpp` file needed; include `Base64.h` and use directly.
- **No dynamic allocation and no size limit in the decoder:** Decoding reads the input in two passes (validate, then decode) without any internal buffer, so encoded strings of any length are handled and the caller's output span is the only limit.
- **Encode table** is `constexpr`. The decode table is built during static initialization.
