#pragma once
// The simulated System functions of the host tests, shared by the shim of every module: the module shims and the shared VHAL.h
// include this header and add what the module needs on top of it.
//
// The clock is simulated. It starts at zero and moves only when a test moves it (VhalSim::AdvanceMs, VhalSim::SetMs) or when a delay
// is called. A delay moves it by whole milliseconds, rounded up, so a loop that only delays still ends. A critical section is a depth
// and a count of enters, and a leave with no open section aborts, as it does on every port. System::Reset records the call in
// VhalSim::resetCalled and does nothing else.
#include <System/System.h>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>


namespace VhalSim {


// The sections currently open, and every enter since the start of the test program.
inline int criticalDepthCount = 0;
inline uint32 criticalEnterCount = 0;

// The simulated clock, in milliseconds.
inline uint64 clockMs = 0;

// Whether System::Reset was called since the flag was last cleared.
inline bool resetCalled = false;


// The number of critical sections currently open.
inline int CriticalDepth() {
	return criticalDepthCount;
}


// Every enter since the start of the test program, so a test can count the sections one call takes.
inline uint32 CriticalEnters() {
	return criticalEnterCount;
}


// Sets the clock. System::GetTick32 reads System::tickLow, which carries the low 32 bits of the clock, so the two are set together.
inline void SetMs(uint64 value) {
	clockMs = value;
	System::tickLow = static_cast<uint32>(value);
}


// Moves the clock forward by delta milliseconds.
inline void AdvanceMs(uint32 delta) {
	SetMs(clockMs + delta);
}


}


// The low 32 bits of the simulated clock. Only SetMs and AdvanceMs write it.
inline volatile uint32 System::tickLow = 0;


inline void System::CriticalSection(bool isEnable) {
	if (isEnable) {
		VhalSim::criticalDepthCount++;
		VhalSim::criticalEnterCount++;
		return;
	}

	if (VhalSim::criticalDepthCount == 0) {
		System::Abort("critical section left without a matching entry", __FILE__, __LINE__);
	}

	VhalSim::criticalDepthCount--;
}


inline uint64 System::GetTick() {
	return VhalSim::clockMs;
}


inline uint64 System::GetMs() {
	return VhalSim::clockMs;
}


inline void System::DelayMs(uint32 delay) {
	VhalSim::AdvanceMs(delay);
}


// The clock counts whole milliseconds, so a delay is rounded up to the next millisecond.
inline void System::DelayUs(uint32 delay) {
	uint32 milliseconds = delay / 1000u;
	if (delay % 1000u != 0u) {
		milliseconds++;
	}

	VhalSim::AdvanceMs(milliseconds);
}


inline bool System::IsInterrupt() {
	return false;
}


inline void System::Reset() {
	VhalSim::resetCalled = true;
}


inline void System::Abort(const char* message, const char* file, uint32 line) {
	std::fprintf(stderr, "sim abort: %s (%s:%" PRIu32 ")\n", message != nullptr ? message : "no message", file != nullptr ? file : "?", line);
	std::abort();
}


inline void System::CriticalError(const char* message, const char* file, uint32 line) {
	System::Abort(message, file, line);
}
