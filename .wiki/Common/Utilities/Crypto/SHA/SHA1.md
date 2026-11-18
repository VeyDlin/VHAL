# SHA1

Header-only SHA-1 hash function (FIPS 180-4) producing a 160-bit (20-byte) digest. The header is `Utilities/Crypto/SHA/SHA1.h`, and the class is `SHA1Hasher`.

**SHA-1 is for protocol use, not for security.** Its use here is the WebSocket handshake (RFC 6455 section 4.2.2), where the hash only proves that the server read the key. Collisions are practical, so do not use SHA-1 for signatures, certificates, message authentication or password storage. Use SHA-256 ([SHA256](SHA256.md)) for those.

The class is not named `SHA1`: the ESP-IDF ROM header declares an unscoped enumerator `SHA1` in the global namespace, which clashes with a global class of that name on the ESP32 port.

## Table of Contents

- [Quick Start](#quick-start)
- [Features](#features)
  - [Hashing a String](#hashing-a-string)
  - [Hashing Binary Data](#hashing-binary-data)
  - [Incremental Updates](#incremental-updates)
  - [Reset and Reuse](#reset-and-reuse)
  - [WebSocket Accept Key](#websocket-accept-key)
- [API Reference](#api-reference)
- [Tests](#tests)

## Quick Start

```cpp
#include <Utilities/Crypto/SHA/SHA1.h>

SHA1Hasher hasher;
hasher.Update(std::string_view("abc"));
std::array<uint8, 20> digest = hasher.Finalize();
```

## Features

The class holds only fixed-size arrays. It never allocates, so it is safe to use on a task with a small stack and no heap.

### Hashing a String

`Update` accepts `std::string_view`, so string literals, `std::string` and views all work.

```cpp
SHA1Hasher hasher;
hasher.Update(std::string_view("password123"));
auto digest = hasher.Finalize(); // std::array<uint8, 20>
```

### Hashing Binary Data

`Update` also accepts `std::span<const uint8>` for raw byte buffers.

```cpp
std::array<uint8, 1000> data = { /* ... */ };

SHA1Hasher hasher;
hasher.Update(std::span(data));
std::array<uint8, 20> digest = hasher.Finalize();
```

### Incremental Updates

Call `Update` several times to feed data in chunks. The digest is the same as for all the data at once, whatever the chunk sizes.

```cpp
SHA1Hasher hasher;
hasher.Update(std::string_view("Part 1"));
hasher.Update(std::string_view("Part 2"));
std::array<uint8, 20> digest = hasher.Finalize();
```

### Reset and Reuse

After `Finalize`, call `Reset` before hashing another message on the same instance.

```cpp
SHA1Hasher hasher;
hasher.Update(std::string_view("First message"));
std::array<uint8, 20> first = hasher.Finalize();

hasher.Reset();
hasher.Update(std::string_view("Second message"));
std::array<uint8, 20> second = hasher.Finalize();
```

### WebSocket Accept Key

The handshake answer is `base64(SHA-1(key + GUID))`, with the GUID `258EAFA5-E914-47DA-95CA-C5AB0DC85B11`. [Base64](../../Encoding/Base/Base64.md) encodes the digest.

```cpp
SHA1Hasher hasher;
hasher.Update(std::string_view("dGhlIHNhbXBsZSBub25jZQ=="));
hasher.Update(std::string_view("258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
std::array<uint8, 20> digest = hasher.Finalize();

char accept[32] = {};
size_t length = Base64::Encode(std::span<char>(accept), std::span<const uint8>(digest));
// accept holds "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" (length 28)
```

## API Reference

### Class `SHA1Hasher`

| Method | Signature | Description |
|---|---|---|
| **Constructor** | `SHA1Hasher()` | Initializes the hasher (calls `Reset` internally). |
| **Reset** | `void Reset()` | Resets internal state so the instance can be reused for a new hash. |
| **Update** | `void Update(std::span<const uint8> data)` | Feeds raw bytes into the hash computation. May be called multiple times. |
| **Update** | `void Update(std::string_view str)` | Feeds a string into the hash computation. Convenience overload that delegates to the span variant. |
| **Finalize** | `std::array<uint8, 20> Finalize()` | Applies SHA-1 padding (a 64-bit big-endian bit length), performs the final transforms and returns the 20-byte digest. Call `Reset` before reusing the instance after this. |

### Private Helpers

These implement the SHA-1 algorithm internals and are not part of the public interface:

| Function | Purpose |
|---|---|
| `Rotl(x, n)` | 32-bit left rotation. |
| `PutByte(byte)` | Appends one byte to the 64-byte block and runs `Transform` when the block is full. Used by `Update` and by the padding in `Finalize`. |
| `Transform()` | Processes one 64-byte block (80-round compression) and updates the state. |

## Tests

`tools/vhal-host-tests/websocket/sha1_tests.cpp` checks the FIPS 180 vectors ("abc", the empty message, the 448-bit message and one million `a`), incremental updates at every length from 0 to 130 bytes (the padding boundaries), `Reset` reuse, and the RFC 6455 accept example.
