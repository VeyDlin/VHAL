#pragma once
#include <Drivers/Interface/VUB/TestKit/VubRandom.h>
#include <Drivers/Interface/VUB/VubBlocks.h>
#include <Drivers/Interface/VUB/VubMeta.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>


// Contract checks a user runs on their own block. Each returns true when every round passes. They use a fixed seed for the random
// data, so a failure repeats. A transform is checked with CheckTransform, a framing with CheckFraming. A link is checked by running
// a stack over a test kit line (see the wiki page of the test kit).


// The guard bytes that follow a buffer span, and their value. A block that writes past its span changes a guard byte, and the check
// fails.
inline constexpr std::size_t VubCanaryBytes = 8;
inline constexpr uint8 VubCanaryValue = 0xC5;


// Writes the guard bytes right after the span of spanLength bytes of buffer.
template<std::size_t Size>
void VubSetCanary(std::array<uint8, Size>& buffer, std::size_t spanLength) {
	for (std::size_t index = 0; index < VubCanaryBytes; index++) {
		buffer[spanLength + index] = VubCanaryValue;
	}
}


// True when the guard bytes right after the span of spanLength bytes of buffer still hold the guard value.
template<std::size_t Size>
bool VubCanaryIntact(const std::array<uint8, Size>& buffer, std::size_t spanLength) {
	for (std::size_t index = 0; index < VubCanaryBytes; index++) {
		if (buffer[spanLength + index] != VubCanaryValue) {
			return false;
		}
	}

	return true;
}


// The transform contract on random packets of 0 to maxPayload bytes (at most 255):
//   - Decode(Encode(x)) gives x back, and the encoded size is at most the length plus growth;
//   - a single flipped bit in the encoded bytes makes Decode return an error;
//   - Encode and Decode write nothing past their spans: Encode gets a span of exactly length + growth bytes, Decode a span of exactly
//     the encoded length, and the guard bytes right after each span are checked after every call.
template<typename Transform>
bool VubCheckTransform(Transform& transform, uint32 seed, std::size_t rounds, std::size_t maxPayload) {
	constexpr std::size_t Room = 255 + Transform::growth;
	VubRandom random(seed);
	VubMeta meta{};

	if (maxPayload > 255) {
		return false;
	}

	for (std::size_t round = 0; round < rounds; round++) {
		std::size_t length = random.Below(static_cast<uint32>(maxPayload + 1));
		std::array<uint8, 255> plain{};
		for (std::size_t index = 0; index < length; index++) {
			plain[index] = static_cast<uint8>(random.Next());
		}

		// Encode into a span of exactly length + growth bytes, with the guard bytes right after it.
		std::array<uint8, Room + VubCanaryBytes> encodeBuffer{};
		std::size_t encodeSpan = length + Transform::growth;
		VubSetCanary(encodeBuffer, encodeSpan);
		std::span<uint8> encodeOut(encodeBuffer.data(), encodeSpan);
		Result<std::size_t> encoded = transform.Encode(meta, std::span<const uint8>(plain.data(), length), encodeOut);
		if (encoded.IsErr() || encoded.Value() > encodeSpan) {
			return false;
		}

		if (!VubCanaryIntact(encodeBuffer, encodeSpan)) {
			return false;
		}

		std::size_t encodedLength = encoded.Value();

		// Decode in place on a span of exactly the encoded length, with the guard bytes right after it.
		std::array<uint8, Room + VubCanaryBytes> decodeBuffer{};
		for (std::size_t index = 0; index < encodedLength; index++) {
			decodeBuffer[index] = encodeBuffer[index];
		}

		VubSetCanary(decodeBuffer, encodedLength);
		Result<std::size_t> decoded = transform.Decode(meta, std::span<uint8>(decodeBuffer.data(), encodedLength));
		if (decoded.IsErr() || decoded.Value() != length) {
			return false;
		}

		for (std::size_t index = 0; index < length; index++) {
			if (decodeBuffer[index] != plain[index]) {
				return false;
			}
		}

		if (!VubCanaryIntact(decodeBuffer, encodedLength)) {
			return false;
		}

		// A flipped bit must be detected: Decode returns an error, and writes nothing past its span.
		if (encodedLength > 0) {
			std::array<uint8, Room + VubCanaryBytes> damaged{};
			for (std::size_t index = 0; index < encodedLength; index++) {
				damaged[index] = encodeBuffer[index];
			}

			VubSetCanary(damaged, encodedLength);
			std::size_t target = random.Below(static_cast<uint32>(encodedLength));
			damaged[target] = static_cast<uint8>(damaged[target] ^ (1u << random.Below(8)));
			Result<std::size_t> refused = transform.Decode(meta, std::span<uint8>(damaged.data(), encodedLength));
			if (refused.IsOk()) {
				return false;
			}

			if (!VubCanaryIntact(damaged, encodedLength)) {
				return false;
			}
		}
	}

	return true;
}


// The framing contract. Random packets of 1 to maxPayload bytes (at most 32) are framed and pushed a byte at a time. With lossPercent
// of the bytes lost on the way:
//   - every framed packet is at most its length plus Growth(length) bytes, the size the stack sizes its framed buffer from;
//   - every packet that comes out is one of the packets sent, in the order sent, and whole;
//   - after the lossy part, three clean frames are sent, and the last two come out exactly.
// Push takes one byte at a time, so chunking is the caller's business and needs no test here.
template<typename Framing>
bool VubCheckFraming(Framing& framing, uint32 seed, std::size_t rounds, std::size_t maxPayload, uint32 lossPercent) {
	constexpr std::size_t MaxSent = 256;
	constexpr std::size_t MaxLength = 32;
	constexpr std::size_t FrameRoom = 2 * (MaxLength + 2) + 8;
	VubRandom random(seed);
	VubMeta meta{};

	if (rounds + 3 > MaxSent || maxPayload == 0 || maxPayload > MaxLength) {
		return false;
	}

	std::array<std::array<uint8, MaxLength>, MaxSent> sent{};
	std::array<std::size_t, MaxSent> sentLength{};
	std::size_t sentCount = 0;

	// The lossy part: every byte is lost with lossPercent. Packets come out as they are decoded; each one is matched to the sent list.
	std::size_t matched = 0;
	std::array<uint8, MaxLength> received{};
	for (std::size_t round = 0; round < rounds; round++) {
		std::size_t length = 1 + random.Below(static_cast<uint32>(maxPayload));
		for (std::size_t index = 0; index < length; index++) {
			sent[sentCount][index] = static_cast<uint8>(random.Next());
		}

		sentLength[sentCount] = length;
		sentCount++;

		std::array<uint8, FrameRoom> frame{};
		Result<std::size_t> framed = framing.Frame(meta, std::span<const uint8>(sent[sentCount - 1].data(), length), frame);
		if (framed.IsErr() || framed.Value() > length + Framing::Growth(length)) {
			return false;
		}

		for (std::size_t index = 0; index < framed.Value(); index++) {
			if (random.Chance(lossPercent)) {
				continue;
			}

			std::size_t out = 0;
			VubFrame state = framing.Push(frame[index], received, out);
			if (state != VubFrame::packet) {
				continue;
			}

			bool found = false;
			for (std::size_t candidate = matched; candidate < sentCount && !found; candidate++) {
				if (sentLength[candidate] != out) {
					continue;
				}

				bool same = true;
				for (std::size_t byteIndex = 0; byteIndex < out && same; byteIndex++) {
					same = sent[candidate][byteIndex] == received[byteIndex];
				}

				if (same) {
					found = true;
					matched = candidate + 1;
				}
			}

			if (!found) {
				return false;
			}
		}
	}

	// The clean part: three frames with no loss. The first may be merged with the lossy part, so only the last two must come out
	// exactly, and any packet that comes out must be the frame being sent.
	for (std::size_t clean = 0; clean < 3; clean++) {
		std::size_t length = 1 + random.Below(static_cast<uint32>(maxPayload));
		for (std::size_t index = 0; index < length; index++) {
			sent[sentCount][index] = static_cast<uint8>(random.Next());
		}

		sentLength[sentCount] = length;
		sentCount++;
		std::size_t current = sentCount - 1;

		std::array<uint8, FrameRoom> frame{};
		Result<std::size_t> framed = framing.Frame(meta, std::span<const uint8>(sent[current].data(), length), frame);
		if (framed.IsErr() || framed.Value() > length + Framing::Growth(length)) {
			return false;
		}

		bool delivered = false;
		for (std::size_t index = 0; index < framed.Value(); index++) {
			std::size_t out = 0;
			VubFrame state = framing.Push(frame[index], received, out);
			if (state != VubFrame::packet) {
				continue;
			}

			bool same = out == sentLength[current];
			for (std::size_t byteIndex = 0; byteIndex < out && same; byteIndex++) {
				same = sent[current][byteIndex] == received[byteIndex];
			}

			if (!same) {
				return false;
			}

			delivered = true;
		}

		if (clean > 0 && !delivered) {
			return false;
		}
	}

	return true;
}
