# AES

A C++20 template-based AES (Advanced Encryption Standard) implementation supporting AES-128, AES-192, and AES-256 in ECB, CBC, and CTR modes.

## Table of Contents

- [Quick Start](#quick-start)
- [Key Sizes](#key-sizes)
- [Operation Modes](#operation-modes)
  - [ECB Mode](#ecb-mode-electronic-codebook)
  - [CBC Mode](#cbc-mode-cipher-block-chaining)
  - [CTR Mode](#ctr-mode-counter)
- [Generic Encrypt/Decrypt](#generic-encryptdecrypt)
- [PKCS7 Padding](#pkcs7-padding)
- [API Reference](#api-reference)

## Quick Start

```cpp
#include <Utilities/Crypto/AES/AES.h>

// Create an AES-128 cipher with a key
std::array<uint8, 16> key = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
                              0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};

AES128 aes(key);

// Encrypt a single block in ECB mode
AES128::Block block = {0x32, 0x43, 0xf6, 0xa8, 0x88, 0x5a, 0x30, 0x8d,
                       0x31, 0x31, 0x98, 0xa2, 0xe0, 0x37, 0x07, 0x34};
aes.EncryptECB(block);

// Decrypt back
aes.DecryptECB(block);
```

## Key Sizes

The key size is selected at compile time via the `AESKeySize` enum and the template parameter:

```cpp
enum class AESKeySize : size_t {
    AES128 = 16,  // 128-bit key, 10 rounds
    AES192 = 24,  // 192-bit key, 12 rounds
    AES256 = 32   // 256-bit key, 14 rounds
};
```

Instantiation with different key sizes:

```cpp
std::array<uint8, 16> key128 = { /* ... */ };
std::array<uint8, 24> key192 = { /* ... */ };
std::array<uint8, 32> key256 = { /* ... */ };

AES<AESKeySize::AES128> aes128(key128);
AES<AESKeySize::AES192> aes192(key192);
AES<AESKeySize::AES256> aes256(key256);

// Or use the convenience type aliases:
AES128 a128(key128);
AES192 a192(key192);
AES256 a256(key256);
```

## Operation Modes

### ECB Mode (Electronic Codebook)

Encrypts/decrypts a single 16-byte block. No IV required. Each block is processed independently.

```cpp
AES128 aes(key);

AES128::Block block = { /* 16 bytes */ };
aes.EncryptECB(block);   // block is now ciphertext
aes.DecryptECB(block);   // block is restored to plaintext
```

### CBC Mode (Cipher Block Chaining)

Processes multi-block data in-place. Requires an IV set beforehand. Data size must be a multiple of 16 bytes (use PKCS7 padding if needed).

```cpp
std::array<uint8, 16> iv = { /* initialization vector */ };

// Option 1: provide IV in constructor
AES128 aes(key, iv);

// Option 2: set IV after construction
AES128 aes2(key);
aes2.SetIV(iv);

// Encrypt 48 bytes (3 blocks) in-place
std::array<uint8, 48> data = { /* ... */ };
aes.EncryptCBC(std::span(data));

// Decrypt in-place (must reset IV to same value before decrypting)
aes.SetIV(iv);
aes.DecryptCBC(std::span(data));
```

### CTR Mode (Counter)

Processes data in-place using a counter derived from the IV/nonce. Data does not need to be a multiple of the block size. Encryption and decryption are the same operation (XOR with keystream).

```cpp
std::array<uint8, 16> nonce = { /* ... */ };
AES256 aes(key256, nonce);

// Encrypt arbitrary-length data in-place
std::array<uint8, 100> data = { /* ... */ };
aes.EncryptCTR(std::span(data));

// Decrypt (reset nonce, then call either EncryptCTR or DecryptCTR)
aes.SetIV(nonce);
aes.DecryptCTR(std::span(data));
```

## Generic Encrypt/Decrypt

The `Encrypt` and `Decrypt` template methods accept any `ByteLike` type (`uint8`, `std::byte`, `char`, `unsigned char`) and select the mode at runtime via the `AESMode` enum:

```cpp
enum class AESMode {
    ECB,
    CBC,  // default
    CTR
};
```

```cpp
AES128 aes(key, iv);

std::array<uint8, 32> data = { /* ... */ };

// Encrypt with CBC (default)
aes.Encrypt(std::span(data));

// Encrypt with a specific mode
aes.Encrypt(std::span(data), AESMode::CTR);

// Works with std::byte spans too
std::array<std::byte, 32> byteData = { /* ... */ };
aes.Encrypt(std::span(byteData), AESMode::ECB);

// Decrypt
aes.SetIV(iv);
aes.Decrypt(std::span(data), AESMode::CBC);
```

## PKCS7 Padding

Static utility methods for PKCS7 padding, useful with ECB and CBC modes where data must be block-aligned.

### AddPKCS7Padding

Adds padding bytes to fill the last block. Returns the new data size, or 0 on error.

```cpp
std::array<uint8, 32> buffer{};

// Copy 20 bytes of real data into buffer
std::copy_n(source, 20, buffer.begin());

// Pad to block boundary: 20 -> 32 (12 padding bytes of value 0x0C)
size_t paddedSize = AES128::AddPKCS7Padding(std::span(buffer), 20);
// paddedSize == 32
```

### RemovePKCS7Padding

Returns the original data size by reading and validating padding bytes. Returns the original span size if padding is invalid.

```cpp
size_t originalSize = AES128::RemovePKCS7Padding(std::span(buffer.data(), paddedSize));
// originalSize == 20
```

### Full CBC Example with Padding

```cpp
AES256 aes(key256, iv);

// Original data: 20 bytes (not block-aligned)
std::array<uint8, 20> original = { /* ... */ };

// Buffer large enough for padded data
std::array<uint8, 32> buffer{};
std::copy(original.begin(), original.end(), buffer.begin());

// Pad
size_t paddedSize = AES256::AddPKCS7Padding(std::span(buffer), original.size());

// Encrypt the padded data
aes.EncryptCBC(std::span(buffer.data(), paddedSize));

// ... transmit or store ...

// Decrypt
aes.SetIV(iv);
aes.DecryptCBC(std::span(buffer.data(), paddedSize));

// Remove padding to recover original size
size_t realSize = AES256::RemovePKCS7Padding(std::span(buffer.data(), paddedSize));
// realSize == 20
```

## API Reference

### Compile-Time Constants

| Constant | Description |
|---|---|
| `KEY_SIZE` | Key size in bytes (16, 24, or 32) |
| `BLOCK_SIZE` | Always 16 bytes (128 bits) |
| `EXPANDED_KEY_SIZE` | Expanded key size (176, 208, or 240) |
| `ROUNDS` | Number of AES rounds (10, 12, or 14) |

### Type Aliases

| Type | Definition |
|---|---|
| `Block` | `std::array<uint8, 16>` |
| `Key` | `std::array<uint8, KEY_SIZE>` |
| `ExpandedKey` | `std::array<uint8, EXPANDED_KEY_SIZE>` |
| `IV` | `std::array<uint8, 16>` |
| `AES128` | `AES<AESKeySize::AES128>` |
| `AES192` | `AES<AESKeySize::AES192>` |
| `AES256` | `AES<AESKeySize::AES256>` |

### Constructors

| Signature | Description |
|---|---|
| `AES(const Key& key)` | Initialize with encryption key only |
| `AES(const Key& key, const IV& iv)` | Initialize with key and IV for CBC/CTR modes |

### Methods

| Method | Description |
|---|---|
| `void SetIV(const IV& iv)` | Set or update the initialization vector |
| `void EncryptECB(Block& block)` | Encrypt a single block in-place (ECB) |
| `void DecryptECB(Block& block)` | Decrypt a single block in-place (ECB) |
| `void EncryptCBC(std::span<uint8> data)` | Encrypt data in-place (CBC). Requires IV. Data size must be a multiple of 16. |
| `void DecryptCBC(std::span<uint8> data)` | Decrypt data in-place (CBC). Requires IV. Data size must be a multiple of 16. |
| `void EncryptCTR(std::span<uint8> data)` | Encrypt data in-place (CTR). Requires IV. Any data size. |
| `void DecryptCTR(std::span<uint8> data)` | Decrypt data in-place (CTR). Same as `EncryptCTR`. |
| `void Encrypt(std::span<T> data, AESMode mode)` | Generic encrypt for any `ByteLike` type. Default mode: CBC. |
| `void Decrypt(std::span<T> data, AESMode mode)` | Generic decrypt for any `ByteLike` type. Default mode: CBC. |
| `static size_t AddPKCS7Padding(std::span<uint8> buffer, size_t dataSize)` | Add PKCS7 padding. Returns padded size or 0 on error. |
| `static size_t RemovePKCS7Padding(std::span<uint8> data)` | Remove PKCS7 padding. Returns unpadded size. |
