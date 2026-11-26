#pragma once
#include <VHAL.h>
#include <Drivers/Interface/Modbus/RTU/RtuFramer.h>
#include <Drivers/Interface/Modbus/RTU/RtuTiming.h>
#include <Utilities/Buffer/RingBuffer.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <Utilities/CompileTime/Require.h>
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <span>


// The settings of an RTU transport, built in the captureless lambda that names the transport:
//
//   using Link = RtuTransport<UARTAdapterG4, GPIOAdapterG4, +[] {
//       return RtuBuilder()
//           .Turnaround(2)
//           .Clock<&MicrosecondClock>();
//   }>;
//
// Rules, checked at compile time:
//   - Turnaround is under 3.5 character times (0 to 3), so the hold ends before the reply can start.
//   - A clock is named with Clock<&Fn>(). Fn returns the microseconds since start, as uint32.
// Lenient() accepts a gap above t1.5 inside a frame. A gap of t3.5 still ends the frame.
class RtuBuilder {
public:
	uint8 turnaroundCharacters = 2;
	bool lenient = false;
	uint32 (*clockFn)() = nullptr;


public:
	constexpr RtuBuilder Turnaround(uint8 characters) const {
		Require<"RTU:turnaround must be under 3.5 character times">(characters <= 3);

		RtuBuilder next = *this;
		next.turnaroundCharacters = characters;
		return next;
	}


	constexpr RtuBuilder Lenient() const {
		RtuBuilder next = *this;
		next.lenient = true;
		return next;
	}


	template<uint32 (*Fn)()>
	constexpr RtuBuilder Clock() const {
		RtuBuilder next = *this;
		next.clockFn = Fn;
		return next;
	}


	constexpr bool Check() const {
		Require<"RTU:clock is missing: add Clock<&Fn>() to the RtuBuilder">(clockFn != nullptr);
		return true;
	}
};


// The RTU transport over one UART and an optional pair of RS-485 pins. Uart is a UARTAdapter port, Pin is its GPIO adapter type,
// Make is the captureless lambda that returns the settings, and RxBufferSize is the number of received bytes the ring holds. The
// ring must hold the largest frame.
//
// The user owns the UART and its interrupt vector. Begin installs the receive callbacks on the adapter, so the vector calls the
// port's existing handler. The receive callback stores each byte and its frame flags in the ring. Send and Receive run in the main
// loop.
template<typename Uart, typename Pin, auto (*Make)(), uint16 RxBufferSize = 256>
class RtuTransport {
public:
	static constexpr RtuBuilder settings = Make();
	static_assert(settings.Check(), "RtuTransport settings are incomplete");
	static_assert(RxBufferSize >= RtuFramer::MaxFrameSize, "the receive ring must hold the largest frame");


private:
	// One received byte. Its flags are set when it is stored: StartsFrame when it starts a frame, Broken when the frame is broken at
	// this byte. Two bytes an entry keeps the default ring at 512 bytes.
	struct RxByte {
		uint8 value;
		uint8 flags;
	};


	static constexpr uint8 StartsFrameFlag = 0x01;
	static constexpr uint8 BrokenFlag = 0x02;


	Uart* uart;
	Pin* dePin;
	Pin* rePin;
	RingBuffer<RxByte, RxBufferSize> ring;
	volatile uint32 lastByteUs = 0;
	volatile bool hasLastByte = false;
	volatile bool frameBroken = false;
	volatile bool errorPending = false;
	uint32 baud = 0;
	uint32 interCharacterUs = 0;
	uint32 interFrameUs = 0;
	bool started = false;


public:
	// dePin drives the transmit enable, high while the frame is on the line. rePin disables the receiver while high, from the start
	// of the send to the end of the hold. Either may be null: a transport without pins does no direction control.
	RtuTransport(Uart& line, Pin* de = nullptr, Pin* re = nullptr): uart(&line), dePin(de), rePin(re) {
	}


	// Reads the baud rate for the gaps, turns on the per-byte receive and replaces the adapter's receive and error callbacks. Checks
	// the clock first: a clock that does not advance returns error, so no gap is ever measured with it. When the adapter refuses
	// the receive mode, the callbacks stay as they are.
	ResultStatus Begin() {
		uint64 startMs = System::GetTick();
		uint32 before = Now();
		while (System::GetTick() - startMs < 2) {
		}

		uint32 after = Now();
		if (before == after) {
			return ResultStatus::error;
		}

		uint32 currentBaud = uart->GetParameters().baudRate;
		if (currentBaud == 0) {
			return ResultStatus::invalidParameter;
		}

		RtuTiming timing = RtuTimingOf(currentBaud);
		baud = currentBaud;
		interCharacterUs = timing.interCharacterUs;
		interFrameUs = timing.interFrameUs;

		ResultStatus status = uart->SetContinuousAsyncRxMode(true);
		if (status != ResultStatus::ok) {
			return status;
		}

		uart->SetOnInterrupt([this](typename Uart::Irq type) {
			if (type == Uart::Irq::Rx) {
				OnRxByte();
			}
		});

		uart->SetOnError([this](typename Uart::Error) {
			OnRxError();
		});

		ClearRx();
		lastByteUs = after;
		started = true;
		return ResultStatus::ok;
	}


	// Sends one frame. Waits for t3.5 of silence since the last byte, clears the receive ring, raises DE (and RE), writes the bytes,
	// waits for the last byte to leave the line, holds the turnaround, then drops DE and RE and clears the ring again. The second
	// clear drops the echo of this frame when the receiver is on. When the line stays busy for timeoutUs, Send returns timeout and
	// sends nothing.
	ResultStatus Send(std::span<const uint8> frame, uint32 timeoutUs) {
		if (!started) {
			return ResultStatus::noInit;
		}

		if (frame.empty()) {
			return ResultStatus::invalidParameter;
		}

		uint32 now = Now();
		uint32 waitStartUs = now;
		while (now - lastByteUs <= interFrameUs) {
			if (now - waitStartUs >= timeoutUs) {
				return ResultStatus::timeout;
			}

			now = Now();
		}

		ClearRx();

		if (dePin != nullptr) {
			dePin->Set();
		}

		if (rePin != nullptr) {
			rePin->Set();
		}

		ResultStatus status = uart->WriteArray(frame.data(), static_cast<uint32>(frame.size()));
		uint32 sentUs = Now();
		uint32 holdUs = RtuCharactersUs(baud, settings.turnaroundCharacters);

		while (Now() - sentUs < holdUs) {
		}

		if (rePin != nullptr) {
			rePin->Reset();
		}

		if (dePin != nullptr) {
			dePin->Reset();
		}

		ClearRx();

		// The bytes may already be on the line, so a failed write is writeError even when the adapter reports a timeout. Only the wait
		// for a quiet line returns timeout, and that path sends nothing.
		return status == ResultStatus::ok ? ResultStatus::ok : ResultStatus::writeError;
	}


	// Waits up to timeoutUs for one complete frame and copies it into frame. A frame is complete when the next byte starts a new
	// frame, or when t3.5 of silence has passed since its last byte. Returns the frame length, timeout when no frame is complete
	// yet, invalidData when the frame broke or does not fit in frame, or noInit before Begin.
	Result<uint16> Receive(std::span<uint8> frame, uint32 timeoutUs) {
		if (!started) {
			return ResultStatus::noInit;
		}

		uint32 startUs = Now();

		while (true) {
			uint16 length = 0;
			ResultStatus status = TakeFrame(frame, length);
			if (status == ResultStatus::ok) {
				return length;
			}

			if (status != ResultStatus::timeout) {
				return status;
			}

			if (timeoutUs == 0 || Now() - startUs >= timeoutUs) {
				return ResultStatus::timeout;
			}
		}
	}


private:
	static uint32 Now() {
		return settings.clockFn();
	}


	// Runs in the receive interrupt, once per byte. Stores the byte with its time and the frame flags. No other work is done here.
	void OnRxByte() {
		uint32 now = Now();
		uint32 gap = now - lastByteUs;
		bool startsFrame = !hasLastByte || gap > interFrameUs;

		if (startsFrame) {
			frameBroken = false;
		} else if (!settings.lenient && gap > interCharacterUs) {
			frameBroken = true;
		}

		if (errorPending) {
			frameBroken = true;
			errorPending = false;
		}

		lastByteUs = now;
		hasLastByte = true;

		uint8 flags = 0;
		if (startsFrame) {
			flags |= StartsFrameFlag;
		}

		if (frameBroken) {
			flags |= BrokenFlag;
		}

		RxByte item{static_cast<uint8>(uart->GetLastRxData()), flags};
		if (ring.Push(item) != ResultStatus::ok) {
			frameBroken = true;
		}
	}


	// Runs in the receive error interrupt. It sets errorPending, which the next OnRxByte consumes. On the G4 port the error interrupt
	// runs before the receive interrupt of the same byte, so that next byte is the one the error belongs to, and OnRxByte stores it with
	// its frame marked broken. The frame is also marked broken here, so an error after the last byte breaks the frame.
	void OnRxError() {
		errorPending = true;
		frameBroken = true;
	}


	// Empties the ring and forgets the frame state. The next byte starts a frame.
	void ClearRx() {
		System::CriticalSection(true);
		ring.Clear();
		hasLastByte = false;
		frameBroken = false;
		errorPending = false;
		System::CriticalSection(false);
	}


	static bool StartsFrame(const RxByte& item) {
		return (item.flags & StartsFrameFlag) != 0;
	}


	static bool Broken(const RxByte& item) {
		return (item.flags & BrokenFlag) != 0;
	}


	// Takes the first frame in the ring when it is complete. Returns timeout while it may still grow, ok with its length, or invalidData
	// when it broke or does not fit in frame. A frame whose last byte is in the ring is complete when the silence after it exceeds t3.5.
	// The ring size, the time of the last byte and the broken flag are read in one critical section, so a byte that arrives meanwhile
	// cannot change the frame between those reads.
	ResultStatus TakeFrame(std::span<uint8> frame, uint16& length) {
		System::CriticalSection(true);
		uint32 now = Now();
		uint16 count = ring.Size();
		bool quiet = now - lastByteUs > interFrameUs;
		bool brokenNow = frameBroken;
		System::CriticalSection(false);

		if (count == 0) {
			return ResultStatus::timeout;
		}

		uint16 frameLength = 1;
		while (frameLength < count && !StartsFrame(ring[frameLength])) {
			frameLength++;
		}

		bool broken = false;
		if (frameLength < count) {
			broken = Broken(ring[frameLength - 1]);
		} else {
			if (!quiet) {
				return ResultStatus::timeout;
			}

			broken = brokenNow;
		}

		if (frameLength > frame.size()) {
			for (uint16 index = 0; index < frameLength; index++) {
				ring.Pop();
			}

			return ResultStatus::invalidData;
		}

		for (uint16 index = 0; index < frameLength; index++) {
			frame[index] = ring.Pop().Value().value;
		}

		length = frameLength;
		return broken ? ResultStatus::invalidData : ResultStatus::ok;
	}
};
