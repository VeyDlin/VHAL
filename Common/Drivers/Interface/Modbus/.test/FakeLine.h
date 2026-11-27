#pragma once
#include <VHAL.h>
#include <Adapter/UARTAdapter.h>
#include "FakeGpio.h"
#include <algorithm>
#include <cstddef>
#include <vector>


class FakeLine;


// Every UART of the simulator. The shim calls FakeLineDue whenever simulated time advances, and each line delivers its due events.
inline std::vector<FakeLine*> simLines;
inline void FakeLineDue();


// A byte the UART sent: its value, the time its last bit left the line, and whether the driver is on (DE high, or no DE pin).
struct FakeSentByte {
	uint8 value;
	uint64 endUs;
	bool driverOn;
};


// A UART on a simulated RS-485 line. It derives from the real UARTAdapter, so it has the same callbacks, the same RX callback shape
// (Irq::Rx with GetLastRxData) and the same GetParameters as a port.
//
// Received bytes and receive errors are events with a time. They are delivered when the simulated clock reaches their time, so the
// receive callback sees each byte at its own time. A send writes its bytes at the baud rate, one character time each. With echo on,
// each sent byte is also received at its end time, unless the receiver is off (RE high) at that time.
class FakeLine: public UARTAdapter<int> {
private:
	struct Event {
		uint64 timeUs;
		uint8 value;
		bool isError;
		Error error;
	};


	std::vector<Event> events;
	std::size_t nextEvent = 0;
	std::vector<FakeSentByte> sent;
	bool echo = false;
	bool rxModeFailure = false;
	bool writeTimeout = false;
	const FakeGpio* dePin = nullptr;
	const FakeGpio* rePin = nullptr;
	FakeLine* peer = nullptr;


public:
	FakeLine() {
		simLines.push_back(this);
		simDue = &FakeLineDue;
	}


	~FakeLine() {
		simLines.erase(std::remove(simLines.begin(), simLines.end(), this), simLines.end());
		if (simLines.empty()) {
			simDue = nullptr;
		}

		if (peer != nullptr && peer->peer == this) {
			peer->peer = nullptr;
		}
	}


	// Joins two lines as one wire: a byte sent on one is received on the other, at the same time, while its driver is on.
	static void Wire(FakeLine& first, FakeLine& second) {
		first.peer = &second;
		second.peer = &first;
	}


	// Echo of each sent byte (a half-duplex line that loops the transmitter back to the receiver).
	void SetEcho(bool on) {
		echo = on;
	}


	// Makes the adapter refuse the continuous receive mode, as a port does when the mode is not available.
	void SetRxModeFailure(bool on) {
		rxModeFailure = on;
	}


	// Makes the write report timeout after every byte has left the line, as the ESP32 port does when its wait for the last byte fails.
	void SetWriteTimeout(bool on) {
		writeTimeout = on;
	}


	// The pins of the transport. The line reads DE for the driver state of each byte and RE for the receiver state.
	void AttachPins(const FakeGpio* de, const FakeGpio* re) {
		dePin = de;
		rePin = re;
	}


	// Schedules a received byte at atUs. Events are kept in time order.
	void ScheduleByte(uint64 atUs, uint8 value) {
		Insert(Event{atUs, value, false, Error::None});
	}


	// Schedules a receive error at atUs (for example Error::Frame or Error::Overrun).
	void ScheduleError(uint64 atUs, Error type) {
		Insert(Event{atUs, 0, true, type});
	}


	const std::vector<FakeSentByte>& Sent() const {
		return sent;
	}


	// True when the receiver is switched off by RE.
	bool ReceiverOff() const {
		return rePin != nullptr && rePin->Level();
	}


	// Delivers every event whose time has been reached. Runs with simInDue set, so the callbacks see the clock without advancing it.
	void Pump() {
		if (simInDue) {
			return;
		}

		simInDue = true;

		while (nextEvent < events.size() && events[nextEvent].timeUs <= simNowUs) {
			Event event = events[nextEvent];
			nextEvent++;

			if (event.isError) {
				CallError(event.error);
			} else {
				lastRxData = event.value;
				CallInterrupt(Irq::Rx);
			}
		}

		simInDue = false;
	}


public:
	ResultStatus Initialization() override {
		return ResultStatus::ok;
	}


	void IrqHandler() override {
	}


	void AbortReceive() override {
	}


	void AbortTransmit() override {
	}


protected:
	ResultStatus StartContinuousAsyncRxMode() override {
		return rxModeFailure ? ResultStatus::error : ResultStatus::ok;
	}


	ResultStatus StopContinuousAsyncRxMode() override {
		return ResultStatus::ok;
	}


	// Writes the bytes one character time each and returns once the last one has left the line, as the ports do. Each byte is recorded
	// with its end time and the driver state, and echoed when the receiver is on.
	ResultStatus WriteByteArray(uint8* buffer, uint32 size) override {
		uint64 start = simNowUs;

		for (uint32 index = 0; index < size; index++) {
			uint64 endUs = start + (static_cast<uint64>(index) + 1) * 11000000ULL / parameters.baudRate;
			simNowUs = endUs;

			bool driverOn = dePin == nullptr || dePin->Level();
			sent.push_back(FakeSentByte{buffer[index], endUs, driverOn});

			if (echo && !ReceiverOff()) {
				ScheduleByte(endUs, buffer[index]);
			}

			if (peer != nullptr && driverOn && !peer->ReceiverOff()) {
				peer->ScheduleByte(endUs, buffer[index]);
			}

			Pump();
		}

		return writeTimeout ? ResultStatus::timeout : ResultStatus::ok;
	}


	ResultStatus ReadByteArray(uint8*, uint32) override {
		return ResultStatus::notSupported;
	}


	ResultStatus WriteByteArrayAsync(uint8*, uint32) override {
		return ResultStatus::notSupported;
	}


	ResultStatus ReadByteArrayAsync(uint8*, uint32) override {
		return ResultStatus::notSupported;
	}


private:
	void Insert(Event event) {
		std::vector<Event>::iterator position = std::upper_bound(events.begin() + static_cast<std::ptrdiff_t>(nextEvent), events.end(), event,
			[](const Event& left, const Event& right) -> bool {
				return left.timeUs < right.timeUs;
			});

		events.insert(position, event);
	}
};


inline void FakeLineDue() {
	for (FakeLine* line : simLines) {
		line->Pump();
	}
}
