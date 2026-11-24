#pragma once
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <vector>


namespace VubBoard {
	// The payloads of one run and the echoes that came back. Each payload starts with its number, two bytes little-endian, so an echo finds
	// its payload whatever order the delivery layer hands the echoes up in. A payload is at least three bytes: two for the number, one more.
	class EchoCheck {
	public:
		static constexpr std::size_t MinPayload = 3;
		static constexpr std::size_t MaxIndex = 65535;


		// Makes count payloads of random sizes from MinPayload to maxPayload, with random content, from the seed.
		EchoCheck(std::size_t count, std::size_t maxPayload, uint32 seed) : random(seed), echoed(count, false) {
			payloads.reserve(count);
			for (std::size_t index = 0; index < count; index++) {
				std::uniform_int_distribution<std::size_t> sizes(MinPayload, maxPayload);
				std::size_t size = sizes(random);
				std::vector<uint8> payload(size);
				payload[0] = static_cast<uint8>(index & 0xFFU);
				payload[1] = static_cast<uint8>((index >> 8) & 0xFFU);
				for (std::size_t byte = 2; byte < size; byte++) {
					payload[byte] = static_cast<uint8>(random() & 0xFFU);
				}

				payloads.push_back(payload);
			}
		}


		std::size_t Count() const {
			return payloads.size();
		}


		std::span<const uint8> Payload(std::size_t index) const {
			return std::span<const uint8>(payloads[index].data(), payloads[index].size());
		}


		// Checks one echo. An echo with the number and the bytes of a payload not yet echoed is counted as echoed. Any other echo is a
		// mismatch (the number or the bytes are wrong), a duplicate (the payload came back before), or unknown (the number is out of range).
		void Accept(std::span<const uint8> echo) {
			if (echo.size() < MinPayload) {
				mismatched++;
				return;
			}

			std::size_t index = static_cast<std::size_t>(echo[0]) | (static_cast<std::size_t>(echo[1]) << 8);
			if (index >= payloads.size()) {
				unknown++;
				return;
			}

			if (echoed[index]) {
				duplicate++;
				return;
			}

			std::span<const uint8> expected = Payload(index);
			if (expected.size() != echo.size()) {
				mismatched++;
				return;
			}

			for (std::size_t byte = 0; byte < expected.size(); byte++) {
				if (expected[byte] != echo[byte]) {
					mismatched++;
					return;
				}
			}

			echoed[index] = true;
			echoedCount++;
		}


		std::size_t Echoed() const {
			return echoedCount;
		}


		std::size_t Mismatched() const {
			return mismatched;
		}


		std::size_t Duplicate() const {
			return duplicate;
		}


		std::size_t Unknown() const {
			return unknown;
		}


	private:
		std::mt19937 random;
		std::vector<std::vector<uint8>> payloads;
		std::vector<bool> echoed;
		std::size_t echoedCount = 0;
		std::size_t mismatched = 0;
		std::size_t duplicate = 0;
		std::size_t unknown = 0;
	};
}
