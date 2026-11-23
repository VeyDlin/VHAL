#pragma once
#include <Drivers/Interface/VUB/Blocks.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>

namespace VUB {


// COBS framing (Cheshire and Baker): a frame is the packet with every zero byte replaced by the distance to the next zero, and the
// frame ends with a zero byte. The frame has no zeros inside, so the zero byte is always a frame end on the wire. A code byte n says
// that n - 1 data bytes follow, and that a zero comes after them, unless n is 255: then 254 data bytes follow and no zero.
//
// LeadingDelimiter adds a zero before each frame too. It flushes noise on the line: whatever came before is ended by that zero. It is a
// template argument, not an option of the Step, because it changes Growth, and Growth is a constant of the type.
//
// The decoder takes one byte at a time and writes the packet into the output as the bytes come. It resynchronises at the next zero
// after garbage: a frame that ends inside a block is broken, and the next frame starts at the next zero. COBS has no check of its own,
// so a damaged frame can come out as another packet of the same bytes; the integrity of the payload is the job of a transform, a CRC.
//
// The wire format is in the wiki page of this header. RAM: a count, a code, a remaining count and two flags.
template<bool LeadingDelimiter>
struct CobsOf {
	static constexpr bool usesTime = false;


	uint8 code = 0;
	uint8 remaining = 0;
	bool pendingZero = false;
	bool overflow = false;
	std::size_t count = 0;


	// The most bytes Frame adds to a packet of length bytes: one code byte per 254 bytes at least, the code of the last block, and the
	// delimiter (and the leading zero when it is on).
	static constexpr std::size_t Growth(std::size_t length) {
		return length + length / 254 + 2 + (LeadingDelimiter ? 1 : 0);
	}


	Result<std::size_t> Frame(const Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (in.empty()) {
			return ResultStatus::invalidParameter;
		}

		if (out.size() < Growth(in.size())) {
			return ResultStatus::messageTooLong;
		}

		std::size_t position = 0;
		if constexpr (LeadingDelimiter) {
			out[position] = 0x00;
			position++;
		}

		// The code of the open block is written when the block ends. A block that a code of 255 ends has no zero after it, so its slot is
		// reserved only when a byte follows it.
		std::size_t codeAt = position;
		position++;
		uint8 code = 1;
		bool slotOpen = true;
		for (uint8 value : in) {
			if (!slotOpen) {
				codeAt = position;
				position++;
				code = 1;
				slotOpen = true;
			}

			if (value == 0x00) {
				out[codeAt] = code;
				codeAt = position;
				position++;
				code = 1;
			} else {
				out[position] = value;
				position++;
				code++;
				if (code == 0xFF) {
					out[codeAt] = code;
					slotOpen = false;
				}
			}
		}

		if (slotOpen) {
			out[codeAt] = code;
		}

		out[position] = 0x00;
		position++;
		return position;
	}


	// Takes one received byte. A zero ends the frame: it is complete when its last block is complete, and broken when a block is still
	// open. A zero between two frames, with no block seen, is nothing.
	FrameStatus Push(uint8 byte, std::span<uint8> out, std::size_t& length) {
		if (byte == 0x00) {
			return Finish(length);
		}

		if (remaining == 0) {
			// A code byte. A zero that the last block owed goes first, because another block follows it.
			if (pendingZero) {
				Store(0x00, out);
				pendingZero = false;
			}

			code = byte;
			remaining = static_cast<uint8>(byte - 1);
			if (remaining == 0) {
				pendingZero = code != 0xFF;
			}
		} else {
			Store(byte, out);
			remaining--;
			if (remaining == 0) {
				pendingZero = code != 0xFF;
			}
		}

		return FrameStatus::pending;
	}


	// Drops a partial frame. The stack calls it when the link changes state.
	void Reset() {
		code = 0;
		remaining = 0;
		pendingZero = false;
		overflow = false;
		count = 0;
	}


private:
	void Store(uint8 value, std::span<uint8> out) {
		if (count < out.size()) {
			out[count] = value;
			count++;
		} else {
			overflow = true;
		}
	}


	FrameStatus Finish(std::size_t& length) {
		if (code == 0) {
			return FrameStatus::pending;
		}

		bool complete = remaining == 0;
		bool tooLong = overflow;
		std::size_t packet = count;
		Reset();
		if (!complete) {
			return FrameStatus::broken;
		}

		if (tooLong) {
			return FrameStatus::tooLong;
		}

		if (packet == 0) {
			return FrameStatus::broken;
		}

		length = packet;
		return FrameStatus::packet;
	}
};


// The standard COBS framing, and the one with a leading zero.
using Cobs = CobsOf<false>;
using CobsFlushed = CobsOf<true>;

}
