#pragma once
#include <System/System.h>
#include <array>
#include <span>
#include <string_view>
#include <algorithm>

/*
SHA-256 cryptographic hash function implementation
Produces a 256-bit (32-byte) hash from input data

Simple hashing:
    SHA256 hasher;
    hasher.Update("Hello World");
    auto hash = hasher.Finalize();

Binary data:
    std::array<uint8, 1000> data = {...};
    SHA256 hasher;
    hasher.Update(std::span(data));
    auto hash = hasher.Finalize();

Multiple updates:
    SHA256 hasher;
    hasher.Update("Part 1");
    hasher.Update("Part 2");
    hasher.Update("Part 3");
    auto finalHash = hasher.Finalize();

Password hashing:
    SHA256 hasher;
    hasher.Update("password123");
    hasher.Update("salt_value");
    auto passwordHash = hasher.Finalize();

File integrity check:
    SHA256 hasher;
    while (readFileChunk(buffer)) {
        hasher.Update(std::span(buffer));
    }
    auto fileHash = hasher.Finalize();

Reset and reuse:
    SHA256 hasher;
    hasher.Update("First message");
    auto hash1 = hasher.Finalize();
    hasher.Reset();
    hasher.Update("Second message");
    auto hash2 = hasher.Finalize();
*/

class SHA256 {
private:
    static constexpr std::array<uint32, 64> K = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };

    std::array<uint32, 8> state{};
    std::array<uint8, 64> buffer{};
    uint64 count = 0;


public:
    SHA256() {
        Reset();
    }

    void Reset() {
        state = {
        	0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
        };
        buffer.fill(0);
        count = 0;
    }

    void Update(std::span<const uint8> data) {
        for (uint8 byte : data) {
            buffer[count % 64] = byte;
            count++;

            if (count % 64 == 0) {
                Transform();
            }
        }
    }

    void Update(std::string_view str) {
        Update(std::span(reinterpret_cast<const uint8*>(str.data()), str.size()));
    }

    std::array<uint8, 32> Finalize() {
        uint64 bitCount = count * 8;

        // Padding
        buffer[count % 64] = 0x80;
        count++;

        // If not enough space for length, add another block
        if (count % 64 > 56) {
            while (count % 64 != 0) {
                buffer[count % 64] = 0;
                count++;
            }
            Transform();
        }

        // Pad to 56 bytes
        while (count % 64 != 56) {
            buffer[count % 64] = 0;
            count++;
        }

        // Append length
        for (int i = 7; i >= 0; --i) {
            buffer[56 + i] = static_cast<uint8>(bitCount >> (8 * (7 - i)));
        }
        count += 8;
        Transform();

        // Produce final hash
        std::array<uint8, 32> hash{};
        for (int i = 0; i < 8; ++i) {
            hash[i * 4] = static_cast<uint8>(state[i] >> 24);
            hash[i * 4 + 1] = static_cast<uint8>(state[i] >> 16);
            hash[i * 4 + 2] = static_cast<uint8>(state[i] >> 8);
            hash[i * 4 + 3] = static_cast<uint8>(state[i]);
        }

        return hash;
    }


private:
    static constexpr uint32 Rotr(uint32 x, int n) {
        return (x >> n) | (x << (32 - n));
    }

    static constexpr uint32 Ch(uint32 x, uint32 y, uint32 z) {
        return (x & y) ^ (~x & z);
    }

    static constexpr uint32 Maj(uint32 x, uint32 y, uint32 z) {
        return (x & y) ^ (x & z) ^ (y & z);
    }

    static constexpr uint32 Sigma0(uint32 x) {
        return Rotr(x, 2) ^ Rotr(x, 13) ^ Rotr(x, 22);
    }

    static constexpr uint32 Sigma1(uint32 x) {
        return Rotr(x, 6) ^ Rotr(x, 11) ^ Rotr(x, 25);
    }

    static constexpr uint32 Gamma0(uint32 x) {
        return Rotr(x, 7) ^ Rotr(x, 18) ^ (x >> 3);
    }

    static constexpr uint32 Gamma1(uint32 x) {
        return Rotr(x, 17) ^ Rotr(x, 19) ^ (x >> 10);
    }

    void Transform() {
        std::array<uint32, 64> W{};

        // Prepare message schedule
        for (int i = 0; i < 16; ++i) {
            W[i] = (static_cast<uint32>(buffer[i * 4]) << 24) |
                   (static_cast<uint32>(buffer[i * 4 + 1]) << 16) |
                   (static_cast<uint32>(buffer[i * 4 + 2]) << 8) |
                   static_cast<uint32>(buffer[i * 4 + 3]);
        }

        for (int i = 16; i < 64; ++i) {
            W[i] = Gamma1(W[i - 2]) + W[i - 7] + Gamma0(W[i - 15]) + W[i - 16];
        }

        // Initialize working variables
        auto [a, b, c, d, e, f, g, h] = state;

        // Main loop
        for (int i = 0; i < 64; ++i) {
            uint32 T1 = h + Sigma1(e) + Ch(e, f, g) + K[i] + W[i];
            uint32 T2 = Sigma0(a) + Maj(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + T2;
        }

        // Add working variables to state
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }
};
