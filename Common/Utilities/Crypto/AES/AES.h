#pragma once

#include <System/System.h>
#include <array>
#include <span>
#include <concepts>
#include <algorithm>
#include <optional>

/*
AES (Advanced Encryption Standard) implementation
Supports AES-128, AES-192, and AES-256 in ECB, CBC, and CTR modes

Basic encryption/decryption:
    std::array<uint8, 16> key = {0x2b, 0x7e, 0x15, 0x16...};
    std::array<uint8, 16> plaintext = {0x32, 0x43, 0xf6, 0xa8...};
    std::array<uint8, 16> ciphertext{};
    
    AES<AESKeySize::AES128> aes(key);
    aes.EncryptBlock(plaintext, ciphertext);
    aes.DecryptBlock(ciphertext, plaintext);

Different key sizes:
    std::array<uint8, 16> key128 = {...};
    std::array<uint8, 24> key192 = {...};
    std::array<uint8, 32> key256 = {...};
    
    AES<AESKeySize::AES128> aes128(key128);
    AES<AESKeySize::AES192> aes192(key192);
    AES<AESKeySize::AES256> aes256(key256);

ECB mode (Electronic Codebook):
    std::array<uint8, 32> data = {...};  // 2 blocks
    std::array<uint8, 32> encrypted{};
    aes.EncryptECB(std::span(data), std::span(encrypted));
    aes.DecryptECB(std::span(encrypted), std::span(data));

CBC mode (Cipher Block Chaining):
    std::array<uint8, 16> iv = {...};  // Initialization vector
    std::array<uint8, 48> data = {...};  // 3 blocks
    std::array<uint8, 48> encrypted{};
    aes.EncryptCBC(std::span(data), std::span(encrypted), iv);
    aes.DecryptCBC(std::span(encrypted), std::span(data), iv);

CTR mode (Counter):
    std::array<uint8, 16> nonce = {...};
    std::array<uint8, 100> data = {...};  // Any size
    std::array<uint8, 100> encrypted{};
    aes.EncryptCTR(std::span(data), std::span(encrypted), nonce);
    aes.DecryptCTR(std::span(encrypted), std::span(data), nonce);

Secure communication:
    AES<AESKeySize::AES256> cipher(sharedKey);
    std::array<uint8, 16> iv = generateRandomIV();
    cipher.EncryptCBC(messageData, encryptedData, iv);
    // Send iv + encryptedData

File encryption:
    AES<AESKeySize::AES128> aes(fileKey);
    while (readFileBlock(buffer)) {
        aes.EncryptCBC(std::span(buffer), std::span(encrypted), iv);
        writeEncryptedBlock(encrypted);
    }

Device authentication:
    AES<AESKeySize::AES128> auth(deviceKey);
    std::array<uint8, 16> challenge = {...};
    std::array<uint8, 16> response{};
    auth.EncryptBlock(challenge, response);
    // Send response to server

Firmware encryption:
    AES<AESKeySize::AES256> firmware(updateKey);
    firmware.EncryptCTR(firmwareData, encryptedFirmware, updateNonce);
*/


// Key sizes
enum class AESKeySize : size_t {
    AES128 = 16,
    AES192 = 24,
    AES256 = 32
};

// Operation modes
enum class AESMode {
    ECB,
    CBC,
    CTR
};

// Concept for byte-like types
template<typename T>
concept ByteLike = std::same_as<T, uint8> || 
                   std::same_as<T, std::byte> || 
                   std::same_as<T, char> ||
                   std::same_as<T, unsigned char>;
template<AESKeySize KeySize = AESKeySize::AES128>
class AES {
public:
    static constexpr size_t KEY_SIZE = static_cast<size_t>(KeySize);
    static constexpr size_t BLOCK_SIZE = 16; // AES block size is always 128 bits (16 bytes)
    
    // Calculate expanded key size based on key size
    static constexpr size_t EXPANDED_KEY_SIZE = []() {
        if constexpr (KeySize == AESKeySize::AES128) return 176;
        else if constexpr (KeySize == AESKeySize::AES192) return 208;
        else return 240; // AES256
    }();
    
    // Number of rounds based on key size
    static constexpr size_t ROUNDS = []() {
        if constexpr (KeySize == AESKeySize::AES128) return 10;
        else if constexpr (KeySize == AESKeySize::AES192) return 12;
        else return 14; // AES256
    }();

    using Block = std::array<uint8, BLOCK_SIZE>;
    using Key = std::array<uint8, KEY_SIZE>;
    using ExpandedKey = std::array<uint8, EXPANDED_KEY_SIZE>;
    using IV = std::array<uint8, BLOCK_SIZE>;

private:
    ExpandedKey roundKey{};
    std::optional<IV> iv;

    // S-box and inverse S-box as constexpr arrays
    static constexpr std::array<uint8, 256> sbox = {
        0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
        0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
        0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
        0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
        0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
        0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
        0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
        0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
        0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
        0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
        0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
        0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
        0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
        0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
        0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
        0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
    };

    static constexpr std::array<uint8, 256> rsbox = {
        0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
        0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
        0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
        0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
        0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
        0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
        0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
        0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
        0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
        0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
        0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
        0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
        0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
        0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
        0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
        0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
    };

    // Rcon for key expansion
    static constexpr std::array<uint8, 11> Rcon = {
        0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
    };

    // Helper functions
    static constexpr uint8 xtime(uint8 x) noexcept {
        return ((x << 1) ^ (((x >> 7) & 1) * 0x1b));
    }

    static constexpr uint8 multiply(uint8 x, uint8 y) noexcept {
        return (((y & 1) * x) ^
                ((y >> 1 & 1) * xtime(x)) ^
                ((y >> 2 & 1) * xtime(xtime(x))) ^
                ((y >> 3 & 1) * xtime(xtime(xtime(x)))) ^
                ((y >> 4 & 1) * xtime(xtime(xtime(xtime(x))))));
    }

    // Key expansion
    void KeyExpansion(const Key& key) noexcept;

    // Core AES operations
    void SubBytes(std::array<std::array<uint8, 4>, 4>& state) const noexcept;
    void InvSubBytes(std::array<std::array<uint8, 4>, 4>& state) const noexcept;
    void ShiftRows(std::array<std::array<uint8, 4>, 4>& state) const noexcept;
    void InvShiftRows(std::array<std::array<uint8, 4>, 4>& state) const noexcept;
    void MixColumns(std::array<std::array<uint8, 4>, 4>& state) const noexcept;
    void InvMixColumns(std::array<std::array<uint8, 4>, 4>& state) const noexcept;
    void AddRoundKey(std::array<std::array<uint8, 4>, 4>& state, uint8 round) const noexcept;

    // Cipher functions
    void Cipher(const uint8* input, uint8* output) const noexcept;
    void InvCipher(const uint8* input, uint8* output) const noexcept;

public:
    // Constructors
    explicit AES(const Key& key) noexcept {
        KeyExpansion(key);
    }

    AES(const Key& key, const IV& initVector) noexcept : iv(initVector) {
        KeyExpansion(key);
    }

    // Set IV for CBC/CTR modes
    void SetIV(const IV& initVector) noexcept {
        iv = initVector;
    }

    // ECB mode
    void EncryptECB(Block& block) const noexcept {
        Cipher(block.data(), block.data());
    }

    void DecryptECB(Block& block) const noexcept {
        InvCipher(block.data(), block.data());
    }

    // CBC mode
    void EncryptCBC(std::span<uint8> data) noexcept;
    void DecryptCBC(std::span<uint8> data) noexcept;

    // CTR mode
    void EncryptCTR(std::span<uint8> data) noexcept;
    void DecryptCTR(std::span<uint8> data) noexcept {
        EncryptCTR(data); // CTR mode encryption and decryption are the same
    }

    // Generic encrypt/decrypt with mode selection
    template<ByteLike T>
    void Encrypt(std::span<T> data, AESMode mode = AESMode::CBC) {
        auto bytes = std::as_writable_bytes(data);
        auto uint8_span = std::span<uint8>(reinterpret_cast<uint8*>(bytes.data()), bytes.size());
        
        switch (mode) {
            case AESMode::ECB:
                for (size_t i = 0; i < uint8_span.size(); i += BLOCK_SIZE) {
                    if (i + BLOCK_SIZE <= uint8_span.size()) {
                        Block block;
                        std::copy_n(uint8_span.begin() + i, BLOCK_SIZE, block.begin());
                        EncryptECB(block);
                        std::copy(block.begin(), block.end(), uint8_span.begin() + i);
                    }
                }
                break;
            case AESMode::CBC:
                EncryptCBC(uint8_span);
                break;
            case AESMode::CTR:
                EncryptCTR(uint8_span);
                break;
        }
    }

    template<ByteLike T>
    void Decrypt(std::span<T> data, AESMode mode = AESMode::CBC) {
        auto bytes = std::as_writable_bytes(data);
        auto uint8_span = std::span<uint8>(reinterpret_cast<uint8*>(bytes.data()), bytes.size());
        
        switch (mode) {
            case AESMode::ECB:
                for (size_t i = 0; i < uint8_span.size(); i += BLOCK_SIZE) {
                    if (i + BLOCK_SIZE <= uint8_span.size()) {
                        Block block;
                        std::copy_n(uint8_span.begin() + i, BLOCK_SIZE, block.begin());
                        DecryptECB(block);
                        std::copy(block.begin(), block.end(), uint8_span.begin() + i);
                    }
                }
                break;
            case AESMode::CBC:
                DecryptCBC(uint8_span);
                break;
            case AESMode::CTR:
                DecryptCTR(uint8_span);
                break;
        }
    }
    
    // PKCS7 padding utilities
    // Add PKCS7 padding to data in buffer
    // Returns new size after padding, or 0 on error
    static size_t AddPKCS7Padding(std::span<uint8> buffer, size_t dataSize) noexcept {
        if (dataSize >= buffer.size()) return 0;
        
        size_t paddingNeeded = BLOCK_SIZE - (dataSize % BLOCK_SIZE);
        if (dataSize + paddingNeeded > buffer.size()) return 0;
        
        // Add padding bytes
        for (size_t i = 0; i < paddingNeeded; i++) {
            buffer[dataSize + i] = static_cast<uint8>(paddingNeeded);
        }
        
        return dataSize + paddingNeeded;
    }
    
    // Remove PKCS7 padding from data
    // Returns size without padding, or original size if padding is invalid
    static size_t RemovePKCS7Padding(std::span<uint8> data) noexcept {
        if (data.empty()) return 0;
        
        uint8 paddingSize = data[data.size() - 1];
        
        // Validate padding size
        if (paddingSize == 0 || paddingSize > BLOCK_SIZE || paddingSize > data.size()) {
            return data.size(); // Invalid padding, return original size
        }
        
        // Verify that all padding bytes have the same value
        for (size_t i = data.size() - paddingSize; i < data.size(); i++) {
            if (data[i] != paddingSize) {
                return data.size(); // Invalid padding, return original size
            }
        }
        
        return data.size() - paddingSize;
    }
};

// Type aliases for common key sizes
using AES128 = AES<AESKeySize::AES128>;
using AES192 = AES<AESKeySize::AES192>;
using AES256 = AES<AESKeySize::AES256>;
