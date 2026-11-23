#pragma once
#include <Drivers/Interface/VUB/VubBlocks.h>
#include <Drivers/Interface/VUB/VubMeta.h>
#include <Drivers/Interface/VUB/VubRequire.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>


// Length-prefixed framing: a sync byte A5, the payload length (Width bytes, 1 or 2, little-endian), a header check byte, then the payload.
// The check byte makes the sum of the sync, the length and itself 00 modulo 256.
//
// A receiver that lost a byte of the header finds a header whose check fails. It drops the sync byte of that header, reads the bytes
// after it again, and looks for a sync byte among them, so it slides one byte at a time until a good header comes. The payload has no
// check: a lost payload byte makes the frame come out short or long, and the bytes of the next frame are taken as payload. The framing
// then resynchronises at the first good header after them. The integrity of the payload is a CRC transform's job.
//
// Width is a template argument, not an option of the Step, because it changes Growth, and Growth is a constant of the type. With one
// byte, a packet must be at most 255 bytes long: a packet bound over 255 is a compile error, through Growth.
//
// The wire format is in the wiki page of this header. RAM: the header window (Width + 2 bytes), a count, the expected length and flags.
template<std::size_t Width>
struct VubLengthPrefix {
	static_assert(Width == 1 || Width == 2, "the length prefix is 1 or 2 bytes");


	static constexpr bool usesTime = false;
	static constexpr uint8 Sync = 0xA5;
	static constexpr std::size_t HeaderBytes = Width + 2;


	std::array<uint8, HeaderBytes> window{};
	std::size_t have = 0;
	std::size_t expected = 0;
	std::size_t count = 0;
	bool inPayload = false;


	// The header: sync, length and check. It does not depend on the length of the payload.
	static constexpr std::size_t Growth(std::size_t length) {
		VubRequire<"a one byte length holds packets of 255 bytes at most: use VubLengthPrefix<2> for a longer packet">(
			Width == 2 || length <= 255);
		return HeaderBytes;
	}


	Result<std::size_t> Frame(const VubMeta&, std::span<const uint8> in, std::span<uint8> out) {
		if (in.empty()) {
			return ResultStatus::invalidParameter;
		}

		if (in.size() > MaxLength() || out.size() < HeaderBytes + in.size()) {
			return ResultStatus::messageTooLong;
		}

		out[0] = Sync;
		uint8 sum = Sync;
		for (std::size_t index = 0; index < Width; index++) {
			uint8 part = static_cast<uint8>(in.size() >> (8 * index));
			out[1 + index] = part;
			sum = static_cast<uint8>(sum + part);
		}

		out[HeaderBytes - 1] = static_cast<uint8>(0 - sum);
		for (std::size_t index = 0; index < in.size(); index++) {
			out[HeaderBytes + index] = in[index];
		}

		return HeaderBytes + in.size();
	}


	// Takes one received byte. Outside a payload the byte goes into the header window, which is checked each time it fills; inside a
	// payload it is stored, and the packet is complete when the length is reached.
	VubFrame Push(uint8 byte, std::span<uint8> out, std::size_t& length) {
		if (inPayload) {
			if (count < out.size()) {
				out[count] = byte;
			}

			count++;
			if (count < expected) {
				return VubFrame::pending;
			}

			inPayload = false;
			if (expected > out.size()) {
				return VubFrame::tooLong;
			}

			length = expected;
			return VubFrame::packet;
		}

		window[have] = byte;
		have++;
		Scan();
		return VubFrame::pending;
	}


	// Drops a partial frame. The stack calls it when the link changes state.
	void Reset() {
		have = 0;
		inPayload = false;
		count = 0;
	}


private:
	static constexpr std::size_t MaxLength() {
		return Width == 1 ? 255 : 65535;
	}


	// Looks at the window until it holds a good header, or it needs more bytes. A window that does not start with the sync drops its
	// first byte; so does a window whose header check fails.
	void Scan() {
		while (have > 0) {
			if (window[0] != Sync) {
				Shift();
				continue;
			}

			if (have < HeaderBytes) {
				return;
			}

			if (!HeaderValid()) {
				Shift();
				continue;
			}

			expected = 0;
			for (std::size_t index = 0; index < Width; index++) {
				expected |= static_cast<std::size_t>(window[1 + index]) << (8 * index);
			}

			have = 0;
			inPayload = true;
			count = 0;
			return;
		}
	}


	// Whether the window holds a whole header: the sum of its bytes is 00 modulo 256, and the length is not zero.
	bool HeaderValid() const {
		uint8 sum = 0;
		for (std::size_t index = 0; index < HeaderBytes; index++) {
			sum = static_cast<uint8>(sum + window[index]);
		}

		bool zeroLength = true;
		for (std::size_t index = 0; index < Width; index++) {
			zeroLength = zeroLength && window[1 + index] == 0;
		}

		return sum == 0 && !zeroLength;
	}


	// Drops the first byte of the window.
	void Shift() {
		for (std::size_t index = 1; index < have; index++) {
			window[index - 1] = window[index];
		}

		have--;
	}
};
