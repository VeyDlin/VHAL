# HMAC

HMAC-SHA256 implementation for message authentication -- verifies integrity and authenticity using a secret key.

## Table of Contents

- [Quick Start](#quick-start)
- [API Reference](#api-reference)
  - [Template Parameters](#template-parameters)
  - [Compute (binary)](#computebinary)
  - [Compute (string)](#computestring)
- [Usage Examples](#usage-examples)
  - [String Messages](#string-messages)
  - [Binary Data](#binary-data)
  - [JWT Token Signing](#jwt-token-signing)
  - [API Request Signing](#api-request-signing)
  - [Truncated Hash Output](#truncated-hash-output)
- [Dependencies](#dependencies)

## Quick Start

```cpp
#include <Utilities/Crypto/HMAC/HMAC.h>

auto hmac = HMAC<32>::Compute("my-secret-key", "Hello World");
// hmac is std::array<uint8, 32>
```

## API Reference

### Template Parameters

```cpp
template<size_t HashSize = 32>
class HMAC;
```

| Parameter  | Default | Description                                      |
|------------|---------|--------------------------------------------------|
| `HashSize` | `32`    | Output size in bytes. 32 for SHA-256, 28 for SHA-224. |

### Internal Constants

| Constant     | Value  | Description              |
|--------------|--------|--------------------------|
| `BLOCK_SIZE` | `64`   | SHA-256 block size       |
| `IPAD`       | `0x36` | Inner padding byte       |
| `OPAD`       | `0x5C` | Outer padding byte       |

### Compute (binary)

```cpp
static std::array<uint8, HashSize> Compute(
    std::span<const uint8> key,
    std::span<const uint8> message
);
```

Computes HMAC over raw binary data. Keys longer than 64 bytes are hashed with SHA-256 first; shorter keys are zero-padded.

### Compute (string)

```cpp
static std::array<uint8, HashSize> Compute(
    std::string_view key,
    std::string_view message
);
```

Convenience overload that accepts string keys and messages. Internally converts to `std::span<const uint8>` and delegates to the binary overload.

## Usage Examples

### String Messages

```cpp
const char* secret = "my-secret-key";
const char* message = "Hello World";
auto hmac = HMAC<32>::Compute(secret, message);
// hmac: std::array<uint8, 32>
```

### Binary Data

```cpp
std::array<uint8, 16> key = {0x01, 0x02, 0x03, /* ... */};
std::array<uint8, 100> data = { /* ... */ };
auto signature = HMAC<32>::Compute(std::span(key), std::span(data));
```

### JWT Token Signing

```cpp
const char* headerPayload = "eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjM0NTY3ODkwIn0";
const char* jwtSecret = "your-jwt-secret";
auto jwtSignature = HMAC<32>::Compute(jwtSecret, headerPayload);
```

### API Request Signing

```cpp
const char* apiKey = "api-secret";
const char* requestBody = "{\"user\":\"john\"}";
auto requestSignature = HMAC<32>::Compute(apiKey, requestBody);
```

### Truncated Hash Output

```cpp
auto hmac256 = HMAC<32>::Compute(key, message);  // Full SHA-256 (32 bytes)
auto hmac224 = HMAC<28>::Compute(key, message);  // Truncated to 28 bytes
```

## Dependencies

- `SHA256` -- from `Utilities/Crypto/SHA/SHA256.h`, used internally for hashing
- `VHAL.h` -- provides `uint8` type alias
- C++20 -- requires `<span>`, `<string_view>`, `<array>`, `<algorithm>`
