#pragma once
// Desktop stand-in for the VHAL umbrella header, for the SoftwareI2C host tests. The real System declaration is kept, so the
// driver compiles unchanged. A virtual clock replaces the timer: DelayUs advances it, and each GetTick call advances it by
// simPollCostUs, so a polling loop ends in simulated time.
#include <System/System.h>
#include <cstdio>
#include <cstdlib>


inline uint64 simNowUs = 0;
inline uint32 simPollCostUs = 0;


inline uint64 System::GetTick() {
	simNowUs += simPollCostUs;
	return simNowUs / 1000;
}


inline void System::DelayUs(uint32 delay) {
	simNowUs += delay;
}


inline void System::Abort(const char *message, const char *file, uint32 line) {
	std::fprintf(stderr, "sim abort: %s (%s:%u)\n", message != nullptr ? message : "no message", file != nullptr ? file : "?", line);
	std::abort();
}
