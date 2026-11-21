#include <VHAL.h>
#include "FakeBus.h"
#include "SimCheck.h"
#include <SoftwareI2C.h>
#include <algorithm>
#include <cstdio>
#include <vector>


// Bit timing on the fake clock. Each SCL rising edge of a transfer is timed, and every interval between two bit clocks must
// equal the bit period. The period is the requested one rounded up to whole microseconds, so the bus never runs faster than
// requested. The low and high halves of each bit, and the bus-free time between a STOP and the next START, must meet the
// minimums of UM10204: Standard mode up to 100 kHz (tLOW 4.7 us, tHIGH 4.0 us, tBUF 4.7 us) and Fast mode above it (tLOW
// 1.3 us, tHIGH 0.6 us, tBUF 1.3 us). Each minimum is rounded up to whole microseconds.


namespace {


struct PhaseMinimums {
	uint64 lowUs;
	uint64 highUs;
	uint64 busFreeUs;
};


// Periods of 10 us and longer are 100 kHz or slower, which is Standard mode.
PhaseMinimums MinimumsFor(uint64 periodUs) {
	if (periodUs >= 10) {
		return PhaseMinimums{ 5, 4, 5 };
	}

	return PhaseMinimums{ 2, 1, 2 };
}


// The bit clocks of a transfer are every rising edge except the last one, which belongs to the STOP.
void CheckBitPeriod(const std::vector<uint64> &rises, uint64 periodUs, const char *label) {
	SIM_CHECK(rises.size() > 3);
	uint64 mismatches = 0;
	for (size_t index = 0; index + 2 < rises.size(); index++) {
		if (rises[index + 1] - rises[index] != periodUs) {
			mismatches++;
		}
	}

	std::printf("%s: %llu of %zu bit intervals differ from %llu us\n", label, static_cast<unsigned long long>(mismatches), rises.size() - 2, static_cast<unsigned long long>(periodUs));
	SIM_CHECK(mismatches == 0);
}


// For each bit, the high half runs from its rising edge to the falling edge that follows, and the low half runs from that fall
// to the next rising edge.
void CheckPhases(uint64 periodUs, const char *label) {
	const std::vector<uint64> &rises = simBus.riseTimesUs;
	const std::vector<uint64> &falls = simBus.fallTimesUs;
	PhaseMinimums minimums = MinimumsFor(periodUs);
	uint64 lowFailures = 0;
	uint64 highFailures = 0;

	for (size_t index = 0; index + 2 < rises.size(); index++) {
		std::vector<uint64>::const_iterator fall = std::upper_bound(falls.begin(), falls.end(), rises[index]);
		if (fall == falls.end() || *fall >= rises[index + 1]) {
			SIM_CHECK(false);
			break;
		}

		uint64 high = *fall - rises[index];
		uint64 low = rises[index + 1] - *fall;
		if (high < minimums.highUs) {
			highFailures++;
		}

		if (low < minimums.lowUs) {
			lowFailures++;
		}
	}

	std::printf("%s: low half under %llu us in %llu bits, high half under %llu us in %llu bits\n", label,
		static_cast<unsigned long long>(minimums.lowUs), static_cast<unsigned long long>(lowFailures),
		static_cast<unsigned long long>(minimums.highUs), static_cast<unsigned long long>(highFailures));
	SIM_EQ(lowFailures, 0);
	SIM_EQ(highFailures, 0);
}


// The time from the first STOP of the trace to the next START, which is the bus-free time between the write and the read.
void CheckBusFree(uint64 minimumUs, const char *label) {
	bool stopSeen = false;
	bool startSeen = false;
	uint64 stopTime = 0;
	uint64 startTime = 0;

	for (const SimEvent &event : simBus.events) {
		if (!stopSeen && event.kind == SimEventKind::Stop) {
			stopSeen = true;
			stopTime = event.timeUs;
		} else if (stopSeen && !startSeen && (event.kind == SimEventKind::Start || event.kind == SimEventKind::RepeatedStart)) {
			startSeen = true;
			startTime = event.timeUs;
		}
	}

	SIM_CHECK(stopSeen && startSeen);
	uint64 busFreeUs = startTime - stopTime;
	std::printf("%s: bus free between STOP and START is %llu us, at least %llu us\n", label, static_cast<unsigned long long>(busFreeUs), static_cast<unsigned long long>(minimumUs));
	SIM_CHECK(busFreeUs >= minimumUs);
}


void CheckRequestedSpeed(uint32 kHz, uint64 periodUs) {
	// The period never gives a faster bus than requested, and is less than one microsecond slower than requested.
	SIM_CHECK(periodUs * kHz >= 1000);
	SIM_CHECK(periodUs * kHz < 1000 + kHz);
}


void MeasureSpeed(uint32 kHz, uint64 periodUs) {
	ResetSim(0);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);
	SIM_EQ(bus.SetFrequency(static_cast<uint16>(kHz)), ResultStatus::ok);

	CheckRequestedSpeed(kHz, periodUs);

	uint8 data[8] = { 0xA5, 0x5A, 0x00, 0xFF, 0x12, 0x34, 0x56, 0x78 };
	simBus.riseTimesUs.clear();
	simBus.fallTimesUs.clear();
	SIM_EQ(bus.WriteByteArray(0x48, 0x1234, 2, data, 8), ResultStatus::ok);
	CheckBitPeriod(simBus.riseTimesUs, periodUs, "write");
	CheckPhases(periodUs, "write");

	// A read without a register address has no repeated START, so its bits are one uniform run. It starts after the STOP of the
	// write, so the bus-free time between them is in the trace.
	simBus.readBytes = { 0x81, 0x42, 0x24, 0x18, 0x66, 0x99, 0xC3, 0x3C };
	simBus.riseTimesUs.clear();
	simBus.fallTimesUs.clear();
	uint8 buffer[8] = { 0 };
	SIM_EQ(bus.ReadByteArray(0x48, 0x0000, 0, buffer, 8), ResultStatus::ok);
	CheckBitPeriod(simBus.riseTimesUs, periodUs, "read");
	CheckPhases(periodUs, "read");
	CheckBusFree(MinimumsFor(periodUs).busFreeUs, "write then read");

	double measuredKHz = 1000.0 / static_cast<double>(periodUs);
	std::printf("requested %4u kHz: bit period %3llu us, measured %8.2f kHz (write and read)\n", static_cast<unsigned>(kHz), static_cast<unsigned long long>(periodUs), measuredKHz);
}


void MeasureSpeedFromParameters() {
	ResetSim(0);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);

	SoftwareI2C::Parameters fast;
	fast.speed = SoftwareI2C::Speed::Fast;
	SIM_EQ(bus.SetParameters(fast), ResultStatus::ok);

	uint8 data[4] = { 0x0F, 0xF0, 0x33, 0xCC };
	simBus.riseTimesUs.clear();
	simBus.fallTimesUs.clear();
	SIM_EQ(bus.WriteByteArray(0x48, 0x01, 1, data, 4), ResultStatus::ok);
	CheckBitPeriod(simBus.riseTimesUs, 3, "Fast from Parameters");
	CheckPhases(3, "Fast from Parameters");
	std::printf("Parameters speed Fast (400 kHz): bit period 3 us, measured 333.33 kHz\n");
}


void RejectedSpeedKeepsThePeriod() {
	ResetSim(0);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);

	SoftwareI2C::Parameters fast;
	fast.speed = SoftwareI2C::Speed::Fast;
	SIM_EQ(bus.SetParameters(fast), ResultStatus::ok);

	// Fast-mode Plus is not applied, so the rejected request must leave the Fast period in place.
	SoftwareI2C::Parameters fastPlus;
	fastPlus.speed = SoftwareI2C::Speed::FastPlus;
	SIM_EQ(bus.SetParameters(fastPlus), ResultStatus::notSupported);

	uint8 data[4] = { 0x0F, 0xF0, 0x33, 0xCC };
	simBus.riseTimesUs.clear();
	SIM_EQ(bus.WriteByteArray(0x48, 0x01, 1, data, 4), ResultStatus::ok);
	CheckBitPeriod(simBus.riseTimesUs, 3, "after rejected Fast-mode Plus");
}


void MeasureDefaultSpeed() {
	ResetSim(0);
	SoftwareI2C bus;
	SIM_EQ(AttachPins(bus), ResultStatus::ok);

	uint8 data[4] = { 0x0F, 0xF0, 0x33, 0xCC };
	simBus.riseTimesUs.clear();
	simBus.fallTimesUs.clear();
	SIM_EQ(bus.WriteByteArray(0x48, 0x01, 1, data, 4), ResultStatus::ok);
	CheckBitPeriod(simBus.riseTimesUs, 10, "default");
	CheckPhases(10, "default");
}


}


int main() {
	MeasureDefaultSpeed();
	MeasureSpeedFromParameters();
	RejectedSpeedKeepsThePeriod();

	MeasureSpeed(10, 100);
	MeasureSpeed(100, 10);
	MeasureSpeed(150, 7);
	MeasureSpeed(200, 5);
	MeasureSpeed(250, 4);
	MeasureSpeed(400, 3);
	return SimCheck::Finish("TestSpeed");
}
