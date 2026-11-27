#pragma once
// Desktop stand-in for the VHAL umbrella header, for the F4 host tests. The real System declaration is kept, so the shared
// headers compile unchanged. Only the System functions the adapters reach are defined here: the tick counter, which
// advances on every read so that the blocking timeouts end, the delay, and the abort paths. Each test includes the register
// model of its peripheral (Stm32F4I2cSim.h, Stm32F4SpiSim.h) before the adapter. GetTick32 reads System::tickLow, which
// only TickHandler writes, so it stays at 0 here: a test that waits in I2CAdapter::Await must complete the transfer first.
#include <System/System.h>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <functional>


// The tick advances on every read, so a poll loop with a timeout ends after the timeout in calls.
inline uint64 simTickCounter = 0;

inline volatile uint32 System::tickLow = 0;

inline std::function<bool(uint32 delay)> System::rtosDelayMsHandle = nullptr;


inline uint64 System::GetTick() {
	return ++simTickCounter;
}


inline void System::DelayUs(uint32) { }


inline void System::Abort(const char* message, const char* file, uint32 line) {
	std::fprintf(stderr, "sim abort: %s (%s:%" PRIu32 ")\n", message != nullptr ? message : "no message", file != nullptr ? file : "?", line);
	std::abort();
}


inline void System::CriticalError(const char* message, const char* file, uint32 line) {
	System::Abort(message, file, line);
}
