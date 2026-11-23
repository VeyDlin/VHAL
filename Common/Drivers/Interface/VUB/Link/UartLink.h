#pragma once
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Receiver.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>

namespace VUB {


// The receive errors a UART link has counted, one count for each kind the port reports.
struct UartErrorCounts {
	uint32 parity;
	uint32 noise;
	uint32 frame;
	uint32 overrun;
};


// A byte-stream link over a UART port: the adapter of Periphery/Adapter/UARTAdapter.h, or one of its ports (on the G431 the adapter
// is UARTAdapterG4, and on the ESP32 it is UARTAdapterESP). The template takes the port type, so each port with the same API works.
//
// The link is a stream, not framed and not reliable, and it is full duplex: either side may start. Received bytes come from the
// port's receive interrupt, one at a time, while the stack's receiver is attached (SetReceiver). The link owns the port's callbacks
// while it has a receiver: Irq::Rx feeds the byte to the receiver, and Irq::Tx ends the transfer the link started. Write copies up to
// TxBytes into the link's own buffer and starts one transfer with WriteArrayAsync. While the transfer runs, Write returns 0. The port
// keeps the pointer it is given, so the copy is needed.
//
// There is no direction pin: the port's RS-485 driver enable is not in this link. A receive error is counted per kind (Errors()), and
// the bytes around it are fed as they come.
//
// MaxPacketBytes is the largest packet of the link, which is the stack's buffer for a packet of the framing above it.
template<typename Uart, std::size_t TxBytes = 64, std::size_t MaxPacketBytes = 64>
class UartLink {
	static_assert(TxBytes > 0, "the transmit buffer of a UART link holds at least one byte");
	static_assert(MaxPacketBytes > 0, "the largest packet of a UART link is at least one byte");

public:
	static constexpr LinkTraits traits = LinkTraits().MaxPacket(MaxPacketBytes).FullDuplex().Starts(Initiator::Either);


private:
	Uart* uart;
	Receiver receiver;
	std::array<uint8, TxBytes> txBuffer{};
	volatile bool txRunning = false;
	bool hooked = false;
	volatile uint32 parityErrors = 0;
	volatile uint32 noiseErrors = 0;
	volatile uint32 frameErrors = 0;
	volatile uint32 overrunErrors = 0;
	ResultStatus receiveStatus = ResultStatus::ok;


public:
	explicit UartLink(Uart& port) : uart(&port) {
	}


	// The port's callbacks capture this link, so it stays where it is.
	UartLink(const UartLink&) = delete;


	UartLink& operator=(const UartLink&) = delete;


	// The stack gives its receiver here in its constructor, and an empty one in its destructor. An attached receiver starts the
	// continuous receive and the callbacks. An empty one stops the receive and clears the callbacks.
	void SetReceiver(Receiver value) {
		receiver = value;
		if (receiver.Attached()) {
			InstallCallbacks();
			receiveStatus = uart->SetContinuousAsyncRxMode(true);
		} else {
			receiveStatus = uart->SetContinuousAsyncRxMode(false);
			RemoveCallbacks();
		}
	}


	// The status of the last SetReceiver: ok, or the status the port refused the continuous receive with.
	ResultStatus ReceiveStatus() const {
		return receiveStatus;
	}


	// Starts one transfer of up to TxBytes of bytes and returns how many it took. Returns 0 while a transfer runs, or while the port
	// is busy with one the link did not start, so the stack keeps the bytes and writes them again later.
	Result<std::size_t> Write(std::span<const uint8> bytes) {
		if (txRunning) {
			return static_cast<std::size_t>(0);
		}

		std::size_t count = bytes.size() < TxBytes ? bytes.size() : TxBytes;
		if (count == 0) {
			return static_cast<std::size_t>(0);
		}

		for (std::size_t index = 0; index < count; index++) {
			txBuffer[index] = bytes[index];
		}

		if (!hooked) {
			InstallCallbacks();
		}

		// The flag is set before the call: a port that completes inside WriteArrayAsync raises Irq::Tx before it returns.
		txRunning = true;
		ResultStatus status = uart->WriteArrayAsync(txBuffer.data(), static_cast<uint32>(count));
		if (status == ResultStatus::ok) {
			return count;
		}

		txRunning = false;
		if (status == ResultStatus::busy) {
			return static_cast<std::size_t>(0);
		}

		return status;
	}


	// The receive errors counted since the link was made.
	UartErrorCounts Errors() const {
		UartErrorCounts counts{};
		counts.parity = parityErrors;
		counts.noise = noiseErrors;
		counts.frame = frameErrors;
		counts.overrun = overrunErrors;
		return counts;
	}


private:
	void InstallCallbacks() {
		uart->SetOnInterrupt([this](typename Uart::Irq irq) {
			OnInterrupt(irq);
		});
		uart->SetOnError([this](typename Uart::Error error) {
			OnError(error);
		});
		hooked = true;
	}


	void RemoveCallbacks() {
		uart->SetOnInterrupt(nullptr);
		uart->SetOnError(nullptr);
		hooked = false;
	}


	// Runs in the port's interrupt: a received byte goes to the receiver, and the end of a transfer clears the running flag.
	void OnInterrupt(typename Uart::Irq irq) {
		if (irq == Uart::Irq::Rx) {
			uint8 byte = static_cast<uint8>(uart->GetLastRxData());
			receiver.Feed(std::span<const uint8>(&byte, 1));
			return;
		}

		txRunning = false;
	}


	void OnError(typename Uart::Error error) {
		if (error == Uart::Error::Parity) {
			parityErrors = parityErrors + 1;
		} else if (error == Uart::Error::Noise) {
			noiseErrors = noiseErrors + 1;
		} else if (error == Uart::Error::Frame) {
			frameErrors = frameErrors + 1;
		} else if (error == Uart::Error::Overrun) {
			overrunErrors = overrunErrors + 1;
		}
	}
};

}
