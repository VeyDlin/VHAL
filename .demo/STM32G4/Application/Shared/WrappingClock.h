#pragma once
#include <cstdint>


// Turns the 32-bit DWT cycle counter into microseconds without a jump at the wrap. The counter wraps every 2^32 cycles (25 s at 170 MHz),
// and a plain division would then step back by about 25 million microseconds. This class counts the wraps, so the value keeps rising.
// The caller must read the counter at least once per wrap, and it must call Microseconds inside a critical section, because the receive
// interrupt reads the clock too.
class WrappingClock {
public:
	// The microseconds of a cycle count, as uint32. The 32-bit result wraps every 71 minutes, which the unsigned differences of the
	// transport handle.
	std::uint32_t Microseconds(std::uint32_t cycles, std::uint32_t cyclesPerUs) {
		if (cycles < lastCycles) {
			wraps++;
		}

		lastCycles = cycles;
		std::uint64_t total = (static_cast<std::uint64_t>(wraps) << 32) | cycles;
		return static_cast<std::uint32_t>(total / cyclesPerUs);
	}


private:
	std::uint32_t lastCycles = 0;
	std::uint32_t wraps = 0;
};
