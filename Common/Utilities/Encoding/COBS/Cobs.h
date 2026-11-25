#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>

namespace Encoding {


// Consistent Overhead Byte Stuffing (Cheshire and Baker). A frame is the packet with every zero byte replaced by the distance to the next
// zero, and the frame ends with a zero byte. The frame has no zero except its last byte, so a zero on the wire is always a frame end.
// A code byte n says that n - 1 data bytes follow, and then a zero, unless n is 255: then 254 data bytes follow and no zero.
//
// The codec has no buffer. Encode writes into the caller's span, and the decoder object writes into the caller's span as the bytes arrive.
class Cobs {
public:
	// The result of a byte pushed to the decoder. pending: the frame goes on. packet: a complete frame, its length is set. broken: the frame
	// ended inside a block, so it is dropped. tooLong: the frame is complete but the output span was too small for it.
	enum class Status : uint8 {
		pending,
		packet,
		broken,
		tooLong
	};


	// Takes one received byte at a time and writes the packet into the caller's span. A zero byte ends a frame; the next frame starts after
	// it. The decoder keeps no packet: only the code of the open block, the count of bytes it still owes, and two flags.
	class Decoder {
	private:
		uint8 code = 0;
		uint8 remaining = 0;
		bool pendingZero = false;
		bool overflow = false;
		std::size_t count = 0;


	public:
		// Sets length when the result is packet. A zero that arrives with no block open is ignored: it is nothing between frames.
		constexpr Status Push(uint8 byte, std::span<uint8> out, std::size_t& length) {
			if (byte == 0x00) {
				return Finish(length);
			}

			if (remaining == 0) {
				// A zero that the last block owed is written ahead of the next block's data.
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

			return Status::pending;
		}


		// Drops a partial frame.
		constexpr void Reset() {
			code = 0;
			remaining = 0;
			pendingZero = false;
			overflow = false;
			count = 0;
		}


	private:
		constexpr void Store(uint8 value, std::span<uint8> out) {
			if (count < out.size()) {
				out[count] = value;
				count++;
			} else {
				overflow = true;
			}
		}


		constexpr Status Finish(std::size_t& length) {
			if (code == 0) {
				return Status::pending;
			}

			bool complete = remaining == 0;
			bool tooLong = overflow;
			std::size_t packet = count;
			Reset();
			if (!complete) {
				return Status::broken;
			}

			if (tooLong) {
				return Status::tooLong;
			}

			length = packet;
			return Status::packet;
		}
	};


	// The most bytes a frame of length bytes takes, delimiter included: one code byte per 254 bytes at most, one code byte for the last
	// block, and the delimiter. Without the delimiter, subtract one.
	static constexpr std::size_t EncodedSize(std::size_t length) {
		return length + length / 254 + 2;
	}


	// Encodes in into out and returns the frame length, delimiter included. out must hold EncodedSize(in.size()) bytes; a smaller span
	// fails with messageTooLong and writes nothing. An empty packet is the frame 0x01 0x00.
	static Result<std::size_t> Encode(std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < EncodedSize(in.size())) {
			return ResultStatus::messageTooLong;
		}

		return EncodeUnchecked(in, out);
	}


	// Encodes in into out as Encode does, without the check: the caller has checked that out holds EncodedSize(in.size()) bytes. Returns the
	// frame length, delimiter included.
	static constexpr std::size_t EncodeUnchecked(std::span<const uint8> in, std::span<uint8> out) {
		// The code of the open block is written when the block ends. A block that a code of 255 ends has no zero after it, so the slot of
		// the next block is reserved only when a byte follows.
		std::size_t position = 1;
		std::size_t codeAt = 0;
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


	// Decodes a frame that is the whole of in: the zero delimiter must be its last byte. Writes the packet into out and returns its length.
	// An empty input is invalidArgument. A frame that is broken or has no delimiter, or a delimiter that is not the last byte, is
	// dataCorrupted. A packet longer than out is bufferOverflow.
	static Result<std::size_t> Decode(std::span<const uint8> in, std::span<uint8> out) {
		if (in.empty()) {
			return ResultStatus::invalidArgument;
		}

		Decoder decoder;
		std::size_t length = 0;
		for (std::size_t index = 0; index < in.size(); index++) {
			Status status = decoder.Push(in[index], out, length);
			if (status == Status::pending) {
				continue;
			}

			if (index + 1 != in.size()) {
				return ResultStatus::dataCorrupted;
			}

			if (status == Status::packet) {
				return length;
			}

			if (status == Status::tooLong) {
				return ResultStatus::bufferOverflow;
			}

			return ResultStatus::dataCorrupted;
		}

		return ResultStatus::dataCorrupted;
	}


	// Decodes a frame in place: the packet is written from the start of frame. This is safe because the packet never catches up with the
	// bytes that are still to be read.
	static Result<std::size_t> Decode(std::span<uint8> frame) {
		return Decode(std::span<const uint8>(frame), frame);
	}
};
}
