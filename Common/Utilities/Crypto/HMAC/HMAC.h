#pragma once
#include <VHAL.h>
#include <Utilities/Crypto/SHA/SHA256.h>
#include <array>
#include <span>
#include <string_view>
#include <algorithm>

template<size_t HashSize = 32>
class HMAC {
private:
    static constexpr size_t BLOCK_SIZE = 64; // SHA-256 block size
    static constexpr uint8 IPAD = 0x36;
    static constexpr uint8 OPAD = 0x5C;


public:
    static std::array<uint8, HashSize> Compute(
        std::span<const uint8> key,
        std::span<const uint8> message) {

        std::array<uint8, BLOCK_SIZE> processedKey{};

        // Process key
        if (key.size() > BLOCK_SIZE) {
            // Hash long keys
            SHA256 hasher;
            hasher.Update(key);
            auto hashedKey = hasher.Finalize();
            std::copy(hashedKey.begin(), hashedKey.end(), processedKey.begin());
        } else {
            // Pad short keys
            std::copy(key.begin(), key.end(), processedKey.begin());
        }

        // Create inner and outer padded keys
        std::array<uint8, BLOCK_SIZE> innerKey{};
        std::array<uint8, BLOCK_SIZE> outerKey{};

        std::transform(processedKey.begin(), processedKey.end(), innerKey.begin(), [](uint8 k) {
        	return k ^ IPAD;
        });
        std::transform(processedKey.begin(), processedKey.end(), outerKey.begin(), [](uint8 k) {
        	return k ^ OPAD;
        });

        // Inner hash: H(K ⊕ ipad || message)
        SHA256 innerHasher;
        innerHasher.Update(std::span(innerKey));
        innerHasher.Update(message);
        auto innerHash = innerHasher.Finalize();

        // Outer hash: H(K ⊕ opad || inner_hash)
        SHA256 outerHasher;
        outerHasher.Update(std::span(outerKey));
        outerHasher.Update(std::span(innerHash));

        return outerHasher.Finalize();
    }


    static std::array<uint8, HashSize> Compute(
        std::string_view key,
        std::string_view message
	) {
        return Compute(
            std::span(reinterpret_cast<const uint8*>(key.data()), key.size()),
            std::span(reinterpret_cast<const uint8*>(message.data()), message.size())
        );
    }
};
