#pragma once
#include <VHAL.h>
#include <array>
#include <span>
#include <string_view>


// SHA-1 (FIPS 180-4). Header-only and heap-free. It is for protocol use (the WebSocket handshake), not for security:
// SHA-1 collisions are practical, so never use it for signatures, certificates or password storage.
// The class is not named SHA1: the ESP-IDF ROM header declares an unscoped enumerator SHA1 in the global namespace.
class SHA1Hasher {
private:
	std::array<uint32, 5> state{};
	std::array<uint8, 64> buffer{};
	uint64 count = 0;


public:
	SHA1Hasher() {
		Reset();
	}


	void Reset() {
		state = {
			0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0
		};
		buffer.fill(0);
		count = 0;
	}


	void Update(std::span<const uint8> data) {
		for (uint8 byte : data) {
			PutByte(byte);
		}
	}


	void Update(std::string_view str) {
		Update(std::span(reinterpret_cast<const uint8*>(str.data()), str.size()));
	}


	// Applies the padding and returns the 20-byte digest. Call Reset() before hashing another message.
	std::array<uint8, 20> Finalize() {
		uint64 bitCount = count * 8;

		PutByte(0x80);
		while (count % 64 != 56) {
			PutByte(0);
		}

		// The length is the last 8 bytes of the final block, big-endian.
		for (int shift = 56; shift >= 0; shift -= 8) {
			PutByte(static_cast<uint8>(bitCount >> shift));
		}

		std::array<uint8, 20> digest{};
		for (uint32 i = 0; i < 5; i++) {
			digest[i * 4] = static_cast<uint8>(state[i] >> 24);
			digest[i * 4 + 1] = static_cast<uint8>(state[i] >> 16);
			digest[i * 4 + 2] = static_cast<uint8>(state[i] >> 8);
			digest[i * 4 + 3] = static_cast<uint8>(state[i]);
		}

		return digest;
	}


private:
	static constexpr uint32 Rotl(uint32 x, int n) {
		return (x << n) | (x >> (32 - n));
	}


	void PutByte(uint8 byte) {
		buffer[count % 64] = byte;
		count++;

		if (count % 64 == 0) {
			Transform();
		}
	}


	void Transform() {
		std::array<uint32, 80> w{};

		for (uint32 i = 0; i < 16; i++) {
			w[i] = (static_cast<uint32>(buffer[i * 4]) << 24) |
				(static_cast<uint32>(buffer[i * 4 + 1]) << 16) |
				(static_cast<uint32>(buffer[i * 4 + 2]) << 8) |
				static_cast<uint32>(buffer[i * 4 + 3]);
		}

		for (uint32 i = 16; i < 80; i++) {
			w[i] = Rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
		}

		uint32 a = state[0];
		uint32 b = state[1];
		uint32 c = state[2];
		uint32 d = state[3];
		uint32 e = state[4];

		for (uint32 i = 0; i < 80; i++) {
			uint32 f = 0;
			uint32 k = 0;

			if (i < 20) {
				f = (b & c) | (~b & d);
				k = 0x5A827999;
			} else if (i < 40) {
				f = b ^ c ^ d;
				k = 0x6ED9EBA1;
			} else if (i < 60) {
				f = (b & c) | (b & d) | (c & d);
				k = 0x8F1BBCDC;
			} else {
				f = b ^ c ^ d;
				k = 0xCA62C1D6;
			}

			uint32 temp = Rotl(a, 5) + f + e + k + w[i];
			e = d;
			d = c;
			c = Rotl(b, 30);
			b = a;
			a = temp;
		}

		state[0] += a;
		state[1] += b;
		state[2] += c;
		state[3] += d;
		state[4] += e;
	}
};
