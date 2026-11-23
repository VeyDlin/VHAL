#pragma once
#include <Drivers/Interface/VUB/TestKit/Random.h>
#include <Drivers/Interface/VUB/Blocks.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Require.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <Utilities/TextLiteral.h>
#include <array>
#include <cstddef>
#include <span>

namespace VUB {


// The compile-time checks of a block call CheckFailed, which the library declares and never defines. A failed check is still a compile
// error: a call to a function that is not constexpr is ill-formed in a constant expression, and the diagnostic prints the message. A
// block's Growth is also called at run time by the checks below, and a call that is not in a constant expression needs a definition, so
// the test kit gives one. It does nothing.
template<TextLiteral Message>
void CheckFailed() {
}


// Contract checks a user runs on their own block. Each returns true when every round passes. They use a fixed seed for the random
// data, so a failure repeats. A transform is checked with CheckTransform, a framing with CheckFraming. A link is checked by running
// a stack over a test kit line (see the wiki page of the test kit).


// The guard bytes that follow a buffer span, and their value. A block that writes past its span changes a guard byte, and the check
// fails.
inline constexpr std::size_t CanaryBytes = 8;
inline constexpr uint8 CanaryValue = 0xC5;


// Writes the guard bytes right after the span of spanLength bytes of buffer.
template<std::size_t Size>
void SetCanary(std::array<uint8, Size>& buffer, std::size_t spanLength) {
	for (std::size_t index = 0; index < CanaryBytes; index++) {
		buffer[spanLength + index] = CanaryValue;
	}
}


// True when the guard bytes right after the span of spanLength bytes of buffer still hold the guard value.
template<std::size_t Size>
bool CanaryIntact(const std::array<uint8, Size>& buffer, std::size_t spanLength) {
	for (std::size_t index = 0; index < CanaryBytes; index++) {
		if (buffer[spanLength + index] != CanaryValue) {
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
bool CheckTransform(Transform& transform, uint32 seed, std::size_t rounds, std::size_t maxPayload) {
	constexpr std::size_t Room = 255 + Transform::growth;
	Random random(seed);
	Meta meta{};

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
		std::array<uint8, Room + CanaryBytes> encodeBuffer{};
		std::size_t encodeSpan = length + Transform::growth;
		SetCanary(encodeBuffer, encodeSpan);
		std::span<uint8> encodeOut(encodeBuffer.data(), encodeSpan);
		Result<std::size_t> encoded = transform.Encode(meta, std::span<const uint8>(plain.data(), length), encodeOut);
		if (encoded.IsErr() || encoded.Value() > encodeSpan) {
			return false;
		}

		if (!CanaryIntact(encodeBuffer, encodeSpan)) {
			return false;
		}

		std::size_t encodedLength = encoded.Value();

		// Decode in place on a span of exactly the encoded length, with the guard bytes right after it.
		std::array<uint8, Room + CanaryBytes> decodeBuffer{};
		for (std::size_t index = 0; index < encodedLength; index++) {
			decodeBuffer[index] = encodeBuffer[index];
		}

		SetCanary(decodeBuffer, encodedLength);
		Result<std::size_t> decoded = transform.Decode(meta, std::span<uint8>(decodeBuffer.data(), encodedLength));
		if (decoded.IsErr() || decoded.Value() != length) {
			return false;
		}

		for (std::size_t index = 0; index < length; index++) {
			if (decodeBuffer[index] != plain[index]) {
				return false;
			}
		}

		if (!CanaryIntact(decodeBuffer, encodedLength)) {
			return false;
		}

		// A flipped bit must be detected: Decode returns an error, and writes nothing past its span.
		if (encodedLength > 0) {
			std::array<uint8, Room + CanaryBytes> damaged{};
			for (std::size_t index = 0; index < encodedLength; index++) {
				damaged[index] = encodeBuffer[index];
			}

			SetCanary(damaged, encodedLength);
			std::size_t target = random.Below(static_cast<uint32>(encodedLength));
			damaged[target] = static_cast<uint8>(damaged[target] ^ (1u << random.Below(8)));
			Result<std::size_t> refused = transform.Decode(meta, std::span<uint8>(damaged.data(), encodedLength));
			if (refused.IsOk()) {
				return false;
			}

			if (!CanaryIntact(damaged, encodedLength)) {
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
bool CheckFraming(Framing& framing, uint32 seed, std::size_t rounds, std::size_t maxPayload, uint32 lossPercent) {
	constexpr std::size_t MaxSent = 256;
	constexpr std::size_t MaxLength = 32;
	constexpr std::size_t FrameRoom = 2 * (MaxLength + 2) + 8;
	Random random(seed);
	Meta meta{};

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
			FrameStatus state = framing.Push(frame[index], received, out);
			if (state != FrameStatus::packet) {
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
			FrameStatus state = framing.Push(frame[index], received, out);
			if (state != FrameStatus::packet) {
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


// The framing contract of a framing with no check of its own (COBS, the length prefix). With loss, a damaged frame can come out as a
// different packet, so only the length is checked there. The framing must resynchronise: after the lossy part, three clean frames are
// sent. The first may be merged with the lossy part and is not checked; the last two must come out exactly.
template<typename Framing>
bool CheckResync(Framing& framing, uint32 seed, std::size_t rounds, std::size_t maxPayload, uint32 lossPercent) {
	constexpr std::size_t MaxSent = 256;
	constexpr std::size_t MaxLength = 32;
	constexpr std::size_t FrameRoom = 2 * (MaxLength + 2) + 8;
	Random random(seed);
	Meta meta{};

	if (rounds + 3 > MaxSent || maxPayload == 0 || maxPayload > MaxLength) {
		return false;
	}

	std::array<std::array<uint8, MaxLength>, MaxSent> sent{};
	std::array<std::size_t, MaxSent> sentLength{};
	std::size_t sentCount = 0;
	std::array<uint8, MaxLength> received{};
	std::size_t receivedLength = 0;

	for (std::size_t round = 0; round < rounds + 3; round++) {
		bool clean = round >= rounds;
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
			if (!clean && random.Chance(lossPercent)) {
				continue;
			}

			FrameStatus state = framing.Push(frame[index], received, receivedLength);
			if (state == FrameStatus::packet) {
				if (receivedLength > maxPayload) {
					return false;
				}

				bool same = receivedLength == sentLength[current];
				for (std::size_t byteIndex = 0; byteIndex < receivedLength && same; byteIndex++) {
					same = sent[current][byteIndex] == received[byteIndex];
				}

				if (clean && round > rounds && !same) {
					return false;
				}

				delivered = delivered || same;
			} else if (clean && round > rounds && state != FrameStatus::pending) {
				return false;
			}
		}

		if (clean && round > rounds && !delivered) {
			return false;
		}
	}

	return true;
}


// The time-aware framing contract, for the gap framing: a frame is sent after a silence of three gaps, its bytes are spaced by less
// than a gap, and the test polls the framing at random between the bytes and after each frame. Without loss, every packet comes out
// whole, in the order sent, and the last one is flushed by the silence after the run. With loss (lossPercent of the bytes lost, the
// time still runs), the lossy part is checked for length only, and the three clean frames after it are checked as CheckResync does.
template<typename Framing>
bool CheckTimedFraming(Framing& framing, uint32 seed, std::size_t rounds, std::size_t maxPayload, uint32 lossPercent, uint32 gapUs) {
	constexpr std::size_t MaxSent = 256;
	constexpr std::size_t MaxLength = 32;
	constexpr std::size_t FrameRoom = MaxLength + 16;
	if (rounds + 3 > MaxSent || maxPayload == 0 || maxPayload > MaxLength || gapUs < 8) {
		return false;
	}

	Random random(seed);
	Meta meta{};
	std::array<std::array<uint8, MaxLength>, MaxSent> sent{};
	std::array<std::size_t, MaxSent> sentLength{};
	std::size_t sentCount = 0;
	std::size_t matched = 0;
	std::array<uint8, MaxLength> received{};
	std::size_t receivedLength = 0;
	uint32 now = 0;

	// How a packet that comes out is checked: next means it must be the next packet sent, current means it must be the frame being sent.
	enum class Check {
		none,
		next,
		current
	};

	auto sameAs = [&](std::size_t index) -> bool {
		if (sentLength[index] != receivedLength) {
			return false;
		}

		for (std::size_t byteIndex = 0; byteIndex < receivedLength; byteIndex++) {
			if (sent[index][byteIndex] != received[byteIndex]) {
				return false;
			}
		}

		return true;
	};

	auto consume = [&](FrameStatus state, Check check, std::size_t current, bool& delivered) -> bool {
		if (state == FrameStatus::packet) {
			if (receivedLength > maxPayload) {
				return false;
			}

			if (check == Check::next) {
				if (matched >= sentCount || !sameAs(matched)) {
					return false;
				}

				matched++;
			} else if (check == Check::current) {
				if (!sameAs(current)) {
					return false;
				}

				delivered = true;
			}

			return true;
		}

		return check == Check::none || state == FrameStatus::pending;
	};

	const std::size_t total = rounds + 3;
	for (std::size_t round = 0; round < total; round++) {
		bool clean = round >= rounds;
		bool requireDelivery = lossPercent != 0 && round > rounds;
		Check check = Check::none;
		if (lossPercent == 0) {
			check = Check::next;
		} else if (round > rounds) {
			check = Check::current;
		}

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

		now += 3 * gapUs;
		bool delivered = false;
		for (std::size_t index = 0; index < framed.Value(); index++) {
			bool lost = !clean && random.Chance(lossPercent);
			now += 1 + random.Below(gapUs / 8);
			if (!lost) {
				FrameStatus state = framing.Push(frame[index], now, received, receivedLength);
				if (!consume(state, check, current, delivered)) {
					return false;
				}
			}

			if (random.Chance(25)) {
				FrameStatus state = framing.Poll(now, received, receivedLength);
				if (!consume(state, check, current, delivered)) {
					return false;
				}
			}
		}

		now += 2 * gapUs;
		if (clean || random.Chance(50)) {
			FrameStatus state = framing.Poll(now, received, receivedLength);
			if (!consume(state, check, current, delivered)) {
				return false;
			}
		}

		if (requireDelivery && !delivered) {
			return false;
		}
	}

	now += 3 * gapUs;
	FrameStatus last = framing.Poll(now, received, receivedLength);
	bool ignored = false;
	if (!consume(last, lossPercent == 0 ? Check::next : Check::none, sentCount - 1, ignored)) {
		return false;
	}

	return lossPercent != 0 || matched == sentCount;
}

}
