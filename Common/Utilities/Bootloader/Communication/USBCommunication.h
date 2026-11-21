#pragma once
#include "../ICommunication.h"
#include <System/System.h>
#include <array>
#include <concepts>
#include <cstddef>
#include <span>


// The buffered function the transport needs. CdcAcmFunction and VendorBulkFunction both have it (see Drivers/Interface/USB/Class).
// IsConfigured() is the session state, and GetSessionGeneration() changes at every configuration and reset, so the transport can see a
// session it did not watch end.
template<typename TFunction>
concept USBBufferedTransportFunction = requires(TFunction& function, std::span<const uint8> data, std::span<uint8> buffer) {
	{ function.Write(data) } -> std::convertible_to<uint16>;
	{ function.Read(buffer) } -> std::convertible_to<uint16>;
	{ function.GetReadableCount() } -> std::convertible_to<uint16>;
	{ function.GetWritableCount() } -> std::convertible_to<uint16>;
	{ function.IsConfigured() } -> std::convertible_to<bool>;
	{ function.GetSessionGeneration() } -> std::convertible_to<uint32>;
};


// A function with a terminal-open signal, such as the DTR line of CDC-ACM. The transport is connected only while it is set too.
template<typename TFunction>
concept USBTerminalTransportFunction = USBBufferedTransportFunction<TFunction> && requires(const TFunction& function) {
	{ function.IsTerminalOpen() } -> std::convertible_to<bool>;
};


// The bootloader's transport over a buffered USB function (CDC-ACM or a vendor bulk function). The function is owned by the application
// and must outlive the transport. The application starts the device; this class does not.
//
// Connection: connected while the function is configured, and also while DTR is set when the function has a terminal-open signal.
// SendData() is all or nothing, which the bootloader relies on: it returns busy and queues nothing when the TX ring has not room for
// the whole packet, and the bootloader sends it again from Process(). A packet larger than the TX ring can never fit, so the ring must
// hold the largest response.
//
// Receive: Process() takes every received byte and hands it to the callback in one or more spans. When the session ends, the transport
// is disconnected or the session changes (a reset or a reconfiguration), the callback gets one empty span, which tells the bootloader
// that the framing is lost. The bytes of the old session are dropped by the function, so the next bytes start a new frame. The empty
// span comes before any byte of the new session, even when the reset is made from the callback itself.
//
// Threading: SendData(), IsReadyToSend() and Process() run in the application's task, the same as the bootloader's calls. The
// callback runs from Process(). The USB callbacks run in interrupt context and touch only the function's rings and state.
template<USBBufferedTransportFunction TFunction>
class USBCommunication : public ICommunication {
public:
	// The bulk packet size of both transports. The bootloader reports it to the host, which keeps its packets within it.
	static constexpr size_t MaxPacketSize = 64;


private:
	TFunction& function;
	DataReceivedCallback dataCallback;
	std::array<uint8, MaxPacketSize> receiveBuffer{};
	uint32 lastGeneration = 0;
	bool lastConnected = false;


public:
	explicit USBCommunication(TFunction& usbFunction)
		: function(usbFunction),
		  lastGeneration(usbFunction.GetSessionGeneration()) {
	}


	ResultStatus Initialize() override {
		return ResultStatus::ok;
	}


	ResultStatus SendData(std::span<const uint8> data) override {
		if (!IsConnected()) {
			return ResultStatus::notReady;
		}

		// Only this task writes the TX ring and the interrupt only drains it, so the room checked here is still there at the write.
		if (data.size() > static_cast<size_t>(function.GetWritableCount())) {
			return ResultStatus::busy;
		}

		if (static_cast<size_t>(function.Write(data)) != data.size()) {
			return ResultStatus::busy;
		}

		return ResultStatus::ok;
	}


	bool IsReadyToSend() const override {
		return IsConnected() && function.GetWritableCount() > 0;
	}


	bool IsConnected() const override {
		if constexpr (USBTerminalTransportFunction<TFunction>) {
			return function.IsConfigured() && function.IsTerminalOpen();
		} else {
			return function.IsConfigured();
		}
	}


	void SetDataReceivedCallback(DataReceivedCallback callback) override {
		dataCallback = callback;
	}


	size_t GetMaxPacketSize() const override {
		return MaxPacketSize;
	}


	void Process() override {
		for (;;) {
			// The connection, the session and one chunk are read in one short section, so a bus reset cannot land between them. A chunk
			// therefore belongs to the session that is current when it is read. The callback runs outside the section.
			System::CriticalSection(true);
			bool connected = IsConnected();
			uint32 generation = function.GetSessionGeneration();
			bool sessionEnded = lastConnected && (generation != lastGeneration || !connected);
			lastGeneration = generation;
			lastConnected = connected;
			uint16 count = 0;
			if (connected && !sessionEnded) {
				count = function.Read(receiveBuffer);
			}

			System::CriticalSection(false);

			// The session ended while the transport was connected: the bootloader's partial frame is lost, so it is told once. The loop
			// goes round again, so the bytes of a session that began since are read only after this empty span.
			if (sessionEnded) {
				Deliver(std::span<const uint8>());
				continue;
			}

			if (count == 0) {
				return;
			}

			Deliver(std::span<const uint8>(receiveBuffer).first(count));
		}
	}


private:
	void Deliver(std::span<const uint8> data) {
		if (dataCallback) {
			dataCallback(data);
		}
	}
};
