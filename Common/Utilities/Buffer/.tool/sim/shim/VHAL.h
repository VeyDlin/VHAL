#pragma once
// Desktop stand-in for the VHAL umbrella header, for the RingBuffer host tests. The real System declaration is kept, so the
// shared header compiles unchanged. Only the System functions the ring buffer reaches are defined here. There are no interrupts
// on the PC, so a section protects nothing by itself, but the shim keeps the depth and counts the enters: a test can check how
// many sections a call takes. A leave at depth 0 has no matching enter on any port, so it aborts.
#include <System/System.h>
#include <cstdio>
#include <cstdlib>


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


inline int SimCriticalDepth() {
	return simCriticalDepth;
}


inline void System::Abort(const char* message, const char* file, uint32 line) {
	std::fprintf(stderr, "sim abort: %s (%s:%u)\n", message != nullptr ? message : "no message", file != nullptr ? file : "?", line);
	std::abort();
}


// The heap asserts that it is not called from an interrupt. The shim runs in no interrupt.
inline bool System::IsInterrupt() {
	return false;
}


inline void System::CriticalError(const char* message, const char* file, uint32 line) {
	System::Abort(message, file, line);
}
