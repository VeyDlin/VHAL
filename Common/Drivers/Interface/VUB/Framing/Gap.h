#pragma once
#include <Drivers/Interface/VUB/Blocks.h>
#include <Drivers/Interface/VUB/Bytes.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Require.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>

namespace VUB {


// Gap framing, as Modbus RTU frames: a frame is the bytes that come with no silence of the gap or longer between them, and a silence of
// the gap or more ends it. Nothing is added to the packet on the wire.
//
// The gap is given in microseconds, or in character times at a baud rate: a character is 11 bits, so 3.5 character times (tenths = 35)
// at 9600 baud is 4011 us. VUB rounds up, so the gap is never shorter than asked; RtuTiming truncates, and gives 4010 us there. The
// baud rate can change at run time (SetBaud, when the UART's parameters change).
//
// Receiving: the stack gives each byte with its time (Push), and polls the framing from Process (Poll), so a frame ends in silence even
// when no byte comes after it. A byte that comes after the gap ends the frame before it; that byte is kept and starts the next frame.
//
// Sending: Ready says whether a frame may start. It is true when the gap has passed since the last byte received and since the
// line was last busy. Sent tells the framing how many bytes the link took. The link's Write says only how many bytes it queued, so the
// framing assumes that the queued bytes leave the line at the baud rate, one after another, from the time they were queued; without a
// baud rate it assumes they leave at once. A wait is bounded by WaitTimeoutUs: after that the frame is sent anyway, so the stack never
// stops for a silent line.
//
// Growth is 0: a frame is the payload. A lost byte in a frame makes it come out short, and the gap cannot tell that: the integrity of
// the payload is a CRC transform's job. The wiki page of this header has the timing rules and the RAM.
// The options of the gap. A gap of 1750 us is the Modbus t3.5 at the baud rates above 19200.
struct GapOptions {
	uint32 gapUs = 1750;
	bool inCharacters = false;
	uint32 tenths = 0;
	uint32 baud = 0;
	uint32 timeoutUs = 100000;
};


// The gap framing with its options as a template argument. The options that do not change are constants of the framing, so the framing
// takes no RAM for them. The baud rate is kept at run time, since SetBaud changes it, and it starts from the options. The stack stores a
// Gap of a chain as Configured, with the options of its chain entry.
template<GapOptions Value = GapOptions{}>
class GapOf {
public:
	using Options = GapOptions;

	template<Options value>
	using Configured = GapOf<value>;

	static constexpr bool usesTime = true;


	// The options of the block, flat after it in the chain: GapUs, GapCharacters, Baud and WaitTimeoutUs.
	template<typename Builder>
	struct Step : Builder {
		explicit constexpr Step(const Builder& base) : Builder(base) {
		}


		constexpr Step GapUs(uint32 value) const {
			Require<"the gap is zero: GapUs() needs a value above 0">(value > 0);
			Options options = this->LastOptions();
			options.gapUs = value;
			options.inCharacters = false;
			return Step(this->WithLastOptions(options));
		}


		// The gap in tenths of a character time, at baud: GapCharacters(35, 9600) is 3.5 character times at 9600 baud.
		constexpr Step GapCharacters(uint32 tenths, uint32 baud) const {
			Require<"the gap is zero: GapCharacters() needs tenths above 0">(tenths > 0);
			Require<"GapCharacters() needs a baud rate above 0">(baud > 0);
			Options options = this->LastOptions();
			options.tenths = tenths;
			options.baud = baud;
			options.inCharacters = true;
			return Step(this->WithLastOptions(options));
		}


		constexpr Step Baud(uint32 value) const {
			Require<"Baud() needs a baud rate above 0">(value > 0);
			Options options = this->LastOptions();
			options.baud = value;
			return Step(this->WithLastOptions(options));
		}


		constexpr Step WaitTimeoutUs(uint32 value) const {
			Require<"WaitTimeoutUs() needs a timeout above 0">(value > 0);
			Options options = this->LastOptions();
			options.timeoutUs = value;
			return Step(this->WithLastOptions(options));
		}
	};


	static constexpr uint32 tenths = Value.inCharacters ? Value.tenths : 0;
	static constexpr uint32 timeoutUs = Value.timeoutUs;
	uint32 gapUs = Value.inCharacters ? CharactersUs(Value.tenths, Value.baud) : Value.gapUs;
	uint32 baud = Value.baud;
	uint32 lastRxUs = 0;
	uint32 txEndUs = 0;
	uint32 waitSinceUs = 0;
	uint32 carryUs = 0;
	std::size_t count = 0;
	uint8 carry = 0;
	bool haveRx = false;
	bool haveTx = false;
	bool waiting = false;
	bool haveCarry = false;
	bool overflow = false;


private:
	// A character is a start bit, 8 data bits, a parity or second stop bit, and a stop bit: 11 bits, as RtuTiming counts them.
	static constexpr uint32 BitsPerCharacter = 11;


public:
	static constexpr std::size_t Growth(std::size_t length) {
		(void)length;
		return 0;
	}


	Result<std::size_t> Frame(const Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (in.empty()) {
			return ResultStatus::invalidParameter;
		}

		if (out.size() < in.size()) {
			return ResultStatus::messageTooLong;
		}

		CopyBytes(in.data(), in.size(), out.data());
		return in.size();
	}


	// Takes one received byte with the time it arrived. A byte that comes after the gap ends the frame before it, and starts the next one.
	FrameStatus Push(uint8 byte, uint32 nowUs, std::span<uint8> out, std::size_t& length) {
		if (haveCarry) {
			Adopt(out);
		}

		if (count > 0 && AtLeast(lastRxUs, nowUs, gapUs)) {
			haveCarry = true;
			carry = byte;
			carryUs = nowUs;
			return Complete(length);
		}

		Store(byte, out);
		lastRxUs = nowUs;
		haveRx = true;
		return FrameStatus::pending;
	}


	// Completes the frame that the silence after its last byte ends. Called from Process with the time of the call.
	FrameStatus Poll(uint32 nowUs, std::span<uint8> out, std::size_t& length) {
		if (haveCarry) {
			Adopt(out);
		}

		if (count == 0 || !AtLeast(lastRxUs, nowUs, gapUs)) {
			return FrameStatus::pending;
		}

		return Complete(length);
	}


	// Whether a frame may start on the line at nowUs: the gap has passed since the last byte received and since the line was busy
	// with the bytes sent. After WaitTimeoutUs of waiting, the answer is true whatever the line does.
	bool Ready(uint32 nowUs) {
		bool receiveQuiet = !haveRx || AtLeast(lastRxUs, nowUs, gapUs);
		bool sendQuiet = !haveTx || AtLeast(txEndUs, nowUs, gapUs);
		if (receiveQuiet && sendQuiet) {
			waiting = false;
			return true;
		}

		if (!waiting) {
			waiting = true;
			waitSinceUs = nowUs;
			return false;
		}

		if (AtLeast(waitSinceUs, nowUs, timeoutUs)) {
			waiting = false;
			return true;
		}

		return false;
	}


	// The link took count bytes of a framed packet at nowUs. They leave the line one after another at the baud rate, from the later of
	// nowUs and the end of the bytes sent before.
	void Sent(uint32 nowUs, std::size_t count) {
		if (count == 0) {
			return;
		}

		uint32 start = nowUs;
		if (haveTx && Due(txEndUs, nowUs)) {
			start = txEndUs;
		}

		txEndUs = start + BytesUs(count, baud);
		haveTx = true;
	}


	// Sets the baud rate at run time. It is the line's rate, used for character times and the send side.
	ResultStatus SetBaud(uint32 value) {
		if (value == 0) {
			return ResultStatus::invalidParameter;
		}

		baud = value;
		if constexpr (tenths != 0) {
			gapUs = CharactersUs(tenths, value);
		}

		return ResultStatus::ok;
	}


	// Drops a partial frame. The stack calls it when the link changes state.
	void Reset() {
		count = 0;
		overflow = false;
		haveCarry = false;
		haveRx = false;
	}


private:
	// The time of tenths character times at baud, in microseconds, from 11 bits per character, rounded up so that the gap is never
	// shorter than asked. The division comes last, so no digit is lost before it. Zero when baud is zero.
	static constexpr uint32 CharactersUs(uint32 tenthCount, uint32 rate) {
		if (rate == 0) {
			return 0;
		}

		uint64 bits = static_cast<uint64>(tenthCount) * BitsPerCharacter * 100000;
		return static_cast<uint32>((bits + rate - 1) / rate);
	}


	// The time of count bytes at baud, in microseconds, rounded up so that an estimate is never short. The division comes last, so no
	// digit is lost before it. Zero when baud is zero.
	static constexpr uint32 BytesUs(std::size_t bytes, uint32 rate) {
		if (rate == 0) {
			return 0;
		}

		uint64 bits = static_cast<uint64>(bytes) * BitsPerCharacter * 1000000;
		return static_cast<uint32>((bits + rate - 1) / rate);
	}


	// Whether the time to is at least span microseconds after from. The difference is signed, so a time that is before from (a byte
	// stamped after the time of the Process call) is not a long silence.
	static bool AtLeast(uint32 from, uint32 to, uint32 span) {
		return static_cast<int32>(to - from) >= static_cast<int32>(span);
	}


	void Store(uint8 value, std::span<uint8> out) {
		if (count < out.size()) {
			out[count] = value;
			count++;
		} else {
			overflow = true;
		}
	}


	// Puts the byte that started the next frame at the front of the frame that is being received.
	void Adopt(std::span<uint8> out) {
		haveCarry = false;
		count = 0;
		Store(carry, out);
		lastRxUs = carryUs;
		haveRx = true;
	}


	// Gives the frame that is being received and clears it.
	FrameStatus Complete(std::size_t& length) {
		bool tooLong = overflow;
		std::size_t packet = count;
		count = 0;
		overflow = false;
		if (tooLong) {
			return FrameStatus::tooLong;
		}

		length = packet;
		return FrameStatus::packet;
	}
};


// The gap framing with the default options.
using Gap = GapOf<>;

}
