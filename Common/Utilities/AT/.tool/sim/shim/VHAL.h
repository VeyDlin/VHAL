#pragma once
// Desktop stand-in for the VHAL umbrella header, for the AT host tests. The real System declaration is kept, so the shared headers
// compile unchanged. The tick is the real System::tickLow, which GetTick32 reads inline. The RTOS delay handle is defined here, since
// System.cpp is not linked. Nothing in the description or the value grammar reads the clock, so the tests do not move the tick.
// A critical section protects nothing on the PC, but the shim keeps the depth and counts the enters, so a test can check the sections
// a call takes.
#include <System/System.h>
#include <cstdio>
#include <cstdlib>
#include <functional>


inline volatile uint32 System::tickLow = 0;

inline std::function<bool(uint32 delay)> System::rtosDelayMsHandle = nullptr;


// The number of critical sections open now. An enter adds one and a leave removes one.
inline int simCriticalDepth = 0;

// Every enter since the start of the test.
inline uint32 simCriticalEnters = 0;


inline void System::CriticalSection(bool isEnable) {
	if (isEnable) {
		simCriticalDepth++;
		simCriticalEnters++;
		return;
	}

	if (simCriticalDepth == 0) {
		System::Abort("critical section left without a matching entry", __FILE__, __LINE__);
	}

	simCriticalDepth--;
}


inline void System::Abort(const char* message, const char* file, uint32 line) {
	std::fprintf(stderr, "sim abort: %s (%s:%u)\n", message != nullptr ? message : "no message", file != nullptr ? file : "?", static_cast<unsigned int>(line));
	std::abort();
}
