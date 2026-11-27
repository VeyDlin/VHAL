#pragma once
#include <VHAL.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>


// A simulated open-drain I2C bus with one target. Pin 0 is SCL and pin 1 is SDA. A line reads low when the master drives it
// low, when a fault holds it, or when the target pulls it low. The target samples SDA on SCL rising edges and drives SDA on
// falling edges, as a real device does. START, repeated START, STOP and every byte go to a trace in wire order.


enum class SimEventKind { Start, RepeatedStart, Stop, Byte };


struct SimEvent {
	SimEventKind kind = SimEventKind::Start;
	uint8 value = 0;
	bool masterWrites = true;
	bool acked = false;
	uint64 timeUs = 0;
};


enum class SimPhase { Idle, Address, Receive, Transmit, Ignore };


struct SimBus {
	// Lines as the master drives them. true means released.
	bool masterScl = true;
	bool masterSda = true;

	// Faults. SDA is held low while the number of SCL rising edges since the reset is at least sdaHeldFromRise and below
	// sdaHeldUntilRise. The default window is empty.
	bool stuckScl = false;
	bool stuckSdaForever = false;
	uint64 sdaHeldFromRise = 0;
	uint64 sdaHeldUntilRise = 0;
	uint64 stretchUs = 0;
	bool failPinConfig = false;

	// Target. It answers to targetDevice, NACKs data byte nackDataIndex of a transfer, and sends readBytes on reads.
	uint8 targetDevice = 0x48;
	bool targetPresent = true;
	int64 nackDataIndex = -1;
	std::vector<uint8> readBytes;

	// Observations.
	uint64 pinCalls = 0;
	uint64 riseCount = 0;
	std::vector<SimEvent> events;
	std::vector<uint64> riseTimesUs;
	std::vector<uint64> fallTimesUs;

	// Target and wire state.
	SimPhase phase = SimPhase::Idle;
	bool inTransfer = false;
	int bitIndex = 0;
	uint8 shift = 0;
	uint8 outByte = 0;
	size_t readIndex = 0;
	int64 dataIndex = 0;
	bool targetSdaLow = false;
	uint64 stretchUntilUs = 0;
	bool lastScl = true;


	// A START or STOP is an SDA edge on the wire while SCL is high, caused by the master. A line that a fault already holds
	// shows no edge, so it gives no START or STOP.
	void SetMaster(uint8 pin, bool released) {
		pinCalls++;
		if (pin == 0) {
			if (released && !masterScl) {
				stretchUntilUs = simNowUs + stretchUs;
			}
			masterScl = released;
		} else {
			bool sclHigh = WireScl();
			bool sdaBefore = WireSda();
			masterSda = released;
			bool sdaAfter = WireSda();

			if (sclHigh && sdaBefore && !sdaAfter) {
				OnStart();
			} else if (sclHigh && !sdaBefore && sdaAfter) {
				OnStop();
			}
		}
		Update();
	}


	bool ReadPin(uint8 pin) {
		pinCalls++;
		Update();
		if (pin == 0) {
			return WireScl();
		}
		return WireSda();
	}


	// Runs the target for every SCL edge that the wire shows since the last call.
	void Update() {
		bool scl = WireScl();

		if (scl && !lastScl) {
			OnRise();
		} else if (!scl && lastScl) {
			OnFall();
		}

		lastScl = scl;
	}


private:
	bool WireScl() const {
		return masterScl && !stuckScl && simNowUs >= stretchUntilUs;
	}


	bool SdaHeld() const {
		return stuckSdaForever || (riseCount >= sdaHeldFromRise && riseCount < sdaHeldUntilRise);
	}


	bool WireSda() const {
		return masterSda && !targetSdaLow && !SdaHeld();
	}


	void Log(SimEventKind kind, uint8 value, bool masterWrites, bool acked) {
		events.push_back(SimEvent{ kind, value, masterWrites, acked, simNowUs });
	}


	uint8 NextReadByte() {
		uint8 value = 0xFF;
		if (readIndex < readBytes.size()) {
			value = readBytes[readIndex];
		}
		readIndex++;
		return value;
	}


	void OnStart() {
		Log(inTransfer ? SimEventKind::RepeatedStart : SimEventKind::Start, 0, true, false);
		inTransfer = true;
		phase = SimPhase::Address;
		bitIndex = 0;
		shift = 0;
		readIndex = 0;
		dataIndex = 0;
		targetSdaLow = false;
	}


	void OnStop() {
		Log(SimEventKind::Stop, 0, true, false);
		inTransfer = false;
		phase = SimPhase::Idle;
		targetSdaLow = false;
	}


	void OnRise() {
		riseCount++;
		if (!inTransfer) {
			return;
		}

		riseTimesUs.push_back(simNowUs);
		bool sample = WireSda();

		if (bitIndex < 8) {
			if (phase != SimPhase::Transmit) {
				shift = static_cast<uint8>((shift << 1) | (sample ? 1 : 0));
			}
			bitIndex++;
			return;
		}

		bool acked = !sample;
		bitIndex = 0;

		if (phase == SimPhase::Address) {
			Log(SimEventKind::Byte, shift, true, acked);
			if (!acked) {
				phase = SimPhase::Ignore;
			} else if ((shift & 1) != 0) {
				phase = SimPhase::Transmit;
				outByte = NextReadByte();
			} else {
				phase = SimPhase::Receive;
			}
		} else if (phase == SimPhase::Receive) {
			Log(SimEventKind::Byte, shift, true, acked);
			if (acked) {
				dataIndex++;
			} else {
				phase = SimPhase::Ignore;
			}
		} else if (phase == SimPhase::Transmit) {
			Log(SimEventKind::Byte, outByte, false, acked);
			if (acked) {
				outByte = NextReadByte();
			} else {
				phase = SimPhase::Ignore;
			}
		}

		shift = 0;
	}


	void OnFall() {
		if (inTransfer) {
			fallTimesUs.push_back(simNowUs);
		}

		if (phase == SimPhase::Idle) {
			return;
		}

		if (bitIndex < 8) {
			targetSdaLow = false;
			if (phase == SimPhase::Transmit) {
				targetSdaLow = ((outByte >> (7 - bitIndex)) & 1) == 0;
			}
			return;
		}

		// Acknowledge slot. The target drives ACK only for a byte it accepts; in a read it releases the line.
		targetSdaLow = false;
		if (phase == SimPhase::Address) {
			targetSdaLow = targetPresent && (shift >> 1) == targetDevice;
		} else if (phase == SimPhase::Receive) {
			targetSdaLow = dataIndex != nackDataIndex;
		}
	}
};


inline SimBus simBus;


class FakeGpio;
using AGPIO = FakeGpio;


class FakeGpio {
public:
	struct IO {
		void *port;
		uint8 pin;
	};

	enum class Mode { Input, Output, OpenDrain };

	struct Parameters {
		Mode mode = Mode::Input;
	};

	uint8 pin = 0;


	FakeGpio() = default;

	FakeGpio(IO &io) : pin(io.pin) { }


	FakeGpio &SetState(bool state) {
		simBus.SetMaster(pin, state);
		return *this;
	}


	FakeGpio &Reset() {
		return SetState(false);
	}


	bool GetState() {
		return simBus.ReadPin(pin);
	}


	ResultStatus SetParameters(Parameters) {
		simBus.pinCalls++;
		if (simBus.failPinConfig) {
			return ResultStatus::hardwareFailure;
		}
		return ResultStatus::ok;
	}
};


// Starts a fresh bus. pollCostUs is the time each GetTick call takes. Use 0 when a test measures bit periods, and a
// non-zero value when a line can stay held, so that the polling loops end.
inline void ResetSim(uint32 pollCostUs) {
	simBus = SimBus{};
	simNowUs = 0;
	simPollCostUs = pollCostUs;
}


// The trace of the bus in wire order: S, Sr, P, and for each byte its value, w (master writes) or r (master reads), and + (ACK)
// or - (NACK). The address byte always counts as written.
inline std::string TraceText() {
	std::string text;
	for (const SimEvent &event : simBus.events) {
		if (!text.empty()) {
			text += ' ';
		}

		if (event.kind == SimEventKind::Start) {
			text += "S";
		} else if (event.kind == SimEventKind::RepeatedStart) {
			text += "Sr";
		} else if (event.kind == SimEventKind::Stop) {
			text += "P";
		} else {
			char token[8];
			std::snprintf(token, sizeof(token), "%02X%c%c", event.value, event.masterWrites ? 'w' : 'r', event.acked ? '+' : '-');
			text += token;
		}
	}
	return text;
}


template <typename BusType>
ResultStatus AttachPins(BusType &bus) {
	AGPIO::IO scl{ nullptr, 0 };
	AGPIO::IO sda{ nullptr, 1 };
	return bus.SetPins(scl, sda);
}
