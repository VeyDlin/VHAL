# JWT -- JSON Web Token for Embedded Systems

A header-only JWT (JSON Web Token) implementation designed for embedded systems with no dynamic memory allocation. Creates and verifies HMAC-SHA256 signed tokens for authentication and data exchange.

---

## Table of Contents

- [Quick Start](#quick-start)
- [Dependencies](#dependencies)
- [Class: JWT](#class-jwt)
  - [Algorithm Enum](#algorithm-enum)
  - [Header Struct](#header-struct)
  - [Claims Struct](#claims-struct)
  - [CreateToken](#createtoken)
  - [VerifyToken (simple)](#verifytoken-simple)
  - [VerifyToken (full)](#verifytoken-full)
  - [IsTokenExpired](#istokenexpired)
  - [IsTokenValid](#istokenvalid)
  - [GetCurrentTime / CreateExpirationTime](#getcurrenttime--createexpirationtime)
- [Custom Claims](#custom-claims)
- [Buffer Sizing Guide](#buffer-sizing-guide)

---

## Quick Start

```cpp
#include <Utilities/Crypto/JWT/JWT.h>

// Create a token
JWT::Header header;
JWT::Claims claims;
claims.sub = "user123";
claims.iat = 1700000000;

std::array<char, 512> tokenBuffer{};
size_t tokenLength;
bool ok = JWT::CreateToken(std::span(tokenBuffer), tokenLength, header, claims, "my-secret");
// tokenBuffer now contains the signed JWT string of length tokenLength

// Verify a token
bool valid = JWT::VerifyToken(
    std::string_view(tokenBuffer.data(), tokenLength),
    "my-secret"
);
```

---

## Dependencies

| Dependency | Path | Purpose |
|---|---|---|
| `HMAC` | `Utilities/Crypto/HMAC/HMAC.h` | HMAC-SHA256 signature computation |
| `SHA256` | `Utilities/Crypto/SHA/SHA256.h` | Hash function used by HMAC |
| `Base64` | `Utilities/Encoding/Base/Base64.h` | Base64 encoding/decoding (converted to Base64URL internally) |
| `JSON` | `Utilities/Serialization/JSON/JSON.h` | JSON serialization and parsing of header/claims |
| `VHAL` | `VHAL.h` | Platform types (`uint8`, `uint32`, `uint64`) |

---

## Class: JWT

A fully static class -- all methods are `static`, no instantiation required.

### Algorithm Enum

```cpp
enum class JWT::Algorithm {
    HS256   // HMAC-SHA256 (only supported algorithm)
};
```

### Header Struct

```cpp
struct JWT::Header {
    Algorithm   alg = Algorithm::HS256;
    std::string_view typ = "JWT";
};
```

The default header produces `{"alg":"HS256","typ":"JWT"}`. In most cases the defaults are sufficient.

### Claims Struct

Standard registered claims following RFC 7519:

| Field | Type | Description |
|---|---|---|
| `iss` | `std::string_view` | Issuer |
| `sub` | `std::string_view` | Subject |
| `aud` | `std::string_view` | Audience |
| `exp` | `uint64` | Expiration time (Unix timestamp). 0 = not set. |
| `nbf` | `uint64` | Not before (Unix timestamp). 0 = not set. |
| `iat` | `uint64` | Issued at (Unix timestamp). 0 = not set. |
| `jti` | `std::string_view` | JWT ID |

Fields set to empty string or `0` are omitted from the generated JSON.

Additionally, up to **10 custom claims** can be added (see [Custom Claims](#custom-claims)).

---

### CreateToken

```cpp
static bool JWT::CreateToken(
    std::span<char> tokenBuffer,   // output buffer for the token string
    size_t& tokenLength,           // receives the length of the written token
    const Header& header,          // JWT header
    const Claims& claims,          // JWT claims (payload)
    std::string_view secret        // HMAC secret key
);
```

Returns `true` on success. The resulting token is written into `tokenBuffer` as a non-null-terminated string of length `tokenLength`.

**Example -- device authentication token:**

```cpp
JWT::Header header;
JWT::Claims claims;
claims.iss = "my-device";
claims.sub = "device-001";
claims.aud = "api-server";
claims.iat = 1700000000;
claims.exp = 1700003600; // +1 hour
claims.AddCustomClaim("device_type", "sensor");
claims.AddCustomClaim("firmware", "v1.2.3");

std::array<char, 1024> token{};
size_t len;
if (JWT::CreateToken(std::span(token), len, header, claims, "secret-key")) {
    // Use std::string_view(token.data(), len)
}
```

---

### VerifyToken (simple)

```cpp
static bool JWT::VerifyToken(
    std::string_view token,
    std::string_view secret
);
```

Verifies the token signature only. Uses internally allocated stack buffers. Returns `true` if the signature is valid.

```cpp
bool valid = JWT::VerifyToken("eyJhbGci...", "my-secret");
```

---

### VerifyToken (full)

```cpp
static bool JWT::VerifyToken(
    std::string_view token,
    std::string_view secret,
    Header& header,                     // receives parsed header
    Claims& claims,                     // receives parsed claims
    std::span<char> headerWorkBuffer,   // work buffer for decoded header JSON
    std::span<char> claimsWorkBuffer,   // work buffer for decoded claims JSON
    std::span<char> claimsStringBuffer1,// storage for iss string
    std::span<char> claimsStringBuffer2,// storage for sub string
    std::span<char> claimsStringBuffer3,// storage for aud string
    std::span<char> claimsStringBuffer4 // storage for jti string
);
```

Verifies the signature and parses the header and claims into the provided structs. The four `claimsStringBuffer` spans provide backing storage for the parsed `string_view` fields (`iss`, `sub`, `aud`, `jti`). The `string_view` members in `claims` will point into these buffers, so the buffers must outlive the `Claims` struct.

**Example:**

```cpp
JWT::Header verifiedHeader;
JWT::Claims verifiedClaims;
std::array<char, 256> headerBuf{};
std::array<char, 512> claimsBuf{};
std::array<char, 64> buf1{}, buf2{}, buf3{}, buf4{};

bool valid = JWT::VerifyToken(
    std::string_view(token.data(), tokenLen), "my-secret",
    verifiedHeader, verifiedClaims,
    std::span(headerBuf), std::span(claimsBuf),
    std::span(buf1), std::span(buf2), std::span(buf3), std::span(buf4)
);

if (valid) {
    // verifiedClaims.sub, verifiedClaims.iss, etc. are now populated
    // verifiedClaims.exp, verifiedClaims.iat hold numeric values
}
```

---

### IsTokenExpired

```cpp
static bool JWT::IsTokenExpired(const Claims& claims, uint64 currentTime = 0);
```

Returns `true` if `currentTime >= claims.exp`. Returns `false` if `exp` is 0 (no expiration set) or `currentTime` is 0 (no clock available).

```cpp
if (JWT::IsTokenExpired(claims, currentTimestamp)) {
    // Token has expired, reject
}
```

---

### IsTokenValid

```cpp
static bool JWT::IsTokenValid(const Claims& claims, uint64 currentTime = 0);
```

Checks both `nbf` (not before) and `exp` (expiration) constraints. Returns `true` if the token is within its valid time window. If `currentTime` is 0, time-based validation is skipped and the method returns `true`.

```cpp
if (JWT::IsTokenValid(claims, currentTimestamp)) {
    // Token is within its valid time window
}
```

---

### GetCurrentTime / CreateExpirationTime

```cpp
static uint64 JWT::GetCurrentTime();
static uint64 JWT::CreateExpirationTime(uint32 secondsFromNow);
```

`GetCurrentTime` returns `0` by default (no RTC available). On systems without a real-time clock, timestamps should be provided externally.

`CreateExpirationTime` adds `secondsFromNow` to the current time. Returns `0` if no clock is available.

---

## Custom Claims

The `Claims` struct supports up to 10 custom key-value string claims.

```cpp
JWT::Claims claims;
claims.AddCustomClaim("role", "admin");
claims.AddCustomClaim("scope", "read_sensors");
claims.AddCustomClaim("device_id", "ABC-123");

// Retrieve a custom claim
std::string_view role = claims.GetCustomClaim("role");    // "admin"
std::string_view missing = claims.GetCustomClaim("foo");  // empty string_view
```

Custom claims are serialized into the JWT payload JSON alongside the standard claims.

---

## Buffer Sizing Guide

All buffers are caller-provided `std::span<char>` to avoid heap allocation. Recommended minimum sizes:

| Buffer | Recommended Size | Purpose |
|---|---|---|
| `tokenBuffer` (CreateToken) | 512-1024 chars | Final token output |
| `headerWorkBuffer` (VerifyToken) | 256 chars | Decoded header JSON |
| `claimsWorkBuffer` (VerifyToken) | 512 chars | Decoded claims JSON |
| `claimsStringBuffer1-4` (VerifyToken) | 64 chars each | Storage for parsed string claims |

Internally, `CreateToken` uses fixed stack buffers: 256 chars for the header JSON, 512 chars for the claims JSON, and 512 chars for the signing payload. Tokens with very large custom claims payloads may require increasing these internal sizes.
