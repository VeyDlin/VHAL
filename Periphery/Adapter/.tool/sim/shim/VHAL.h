#pragma once
// Desktop stand-in for the VHAL umbrella header, for the adapter base class host tests. The real System declaration is kept,
// so the shared headers compile unchanged. Only the System functions the tests reach are defined here. There are no interrupts
// on the PC, so a critical section protects nothing by itself, but the shim keeps the depth and counts the enters.
// A leave at depth 0 has no matching enter on any port, so it aborts.
// The tick is System::tickLow, which GetTick32 reads inline as it does on a port. A test that needs time passing runs a thread
// that advances it once per millisecond, as SysTick would. The RTOS delay handle is defined here because System.cpp is not linked.
#include <System/System.h>
#include <cstdio>
#include <cstdlib>
#include <functional>


inline volatile uint32 System::tickLow = 0;

inline std::function<bool(uint32 delay)> System::rtosDelayMsHandle = nullptr;


// The number of critical sections open now. An enter adds one and a leave removes one.
inline int simCriticalDepth = 0;

// Every enter since the start of the test, so a test can count the sections one call takes.
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
	std::fprintf(stderr, "sim abort: %s (%s:%u)\n", message != nullptr ? message : "no message", file != nullptr ? file : "?", line);
	std::abort();
}


inline void System::CriticalError(const char* message, const char* file, uint32 line) {
	System::Abort(message, file, line);
}
