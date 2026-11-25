#pragma once
#include <Drivers/Interface/VUB/Blocks.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Encoding/COBS/Cobs.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>

namespace VUB {


// COBS framing (Cheshire and Baker) for a byte stream: the frame is made and read by the shared codec (Utilities/Encoding/COBS/Cobs.h).
// This framing adds what a stack needs on top of it: the growth of a frame, the refusal of an empty packet, and the frame status.
//
// LeadingDelimiter adds a zero ahead of each frame too. It flushes noise on the line: whatever came ahead of it is ended by that zero.
// It is a template argument, not an option of the Step, because it changes Growth, and Growth is a constant of the type.
//
// The decoder takes one byte at a time and writes the packet into the output as the bytes come. It resynchronises at the next zero
// after garbage: a frame that ends inside a block is broken, and the next frame starts at the next zero. COBS has no check of its own,
// so a damaged frame can come out as another packet of the same bytes; the integrity of the payload is the job of a transform, a CRC.
//
// The wire format is in the wiki page of this header.
template<bool LeadingDelimiter>
struct CobsOf {
	static constexpr bool usesTime = false;


private:
	Encoding::Cobs::Decoder decoder;


public:
	// The most bytes Frame adds to a packet of length bytes: one code byte per 254 bytes at most, the code of the last block, and the
	// delimiter (and the leading zero when it is on).
	static constexpr std::size_t Growth(std::size_t length) {
		return Encoding::Cobs::EncodedSize(length) - length + (LeadingDelimiter ? 1 : 0);
	}


	Result<std::size_t> Frame(const Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (in.empty()) {
			return ResultStatus::invalidParameter;
		}

		if (out.size() < in.size() + Growth(in.size())) {
			return ResultStatus::messageTooLong;
		}

		std::size_t position = 0;
		if constexpr (LeadingDelimiter) {
			out[position] = 0x00;
			position++;
		}

		return position + Encoding::Cobs::EncodeUnchecked(in, out.subspan(position));
	}


	// Takes one received byte. A zero ends the frame: it is complete when its last block is complete, and broken when a block is still
	// open. A zero between two frames, with no block seen, is nothing. A frame with no data is broken.
	FrameStatus Push(uint8 byte, std::span<uint8> out, std::size_t& length) {
		Encoding::Cobs::Status status = decoder.Push(byte, out, length);
		if (status == Encoding::Cobs::Status::packet && length == 0) {
			return FrameStatus::broken;
		}

		return FrameOf(status);
	}


	// Drops a partial frame. The stack calls it when the link changes state.
	void Reset() {
		decoder.Reset();
	}


private:
	static FrameStatus FrameOf(Encoding::Cobs::Status status) {
		if (status == Encoding::Cobs::Status::packet) {
			return FrameStatus::packet;
		}

		if (status == Encoding::Cobs::Status::broken) {
			return FrameStatus::broken;
		}

		if (status == Encoding::Cobs::Status::tooLong) {
			return FrameStatus::tooLong;
		}

		return FrameStatus::pending;
	}
};


// The standard COBS framing, and the one with a leading zero.
using Cobs = CobsOf<false>;
using CobsFlushed = CobsOf<true>;

}
