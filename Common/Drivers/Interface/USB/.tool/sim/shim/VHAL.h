#pragma once
// Desktop stand-in for the VHAL umbrella header (Periphery/VHAL.h needs a port and VHALConfig.h, which a PC build does
// not have). The real System declaration is kept, so the shared headers compile unchanged. Only the System functions the
// host tests reach are defined here. There are no interrupts on the PC, so a critical section protects nothing by itself,
// but the shim keeps the depth the target keeps: sections nest, and only the outermost leave restores the interrupt state
// the outermost enter found. A leave at depth 0 has no matching enter on any port, so it aborts. A test calls
// SimAssertNoCriticalSection() where no section may be open, so a section that a function left open fails the test.
#include <System/System.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>


// The number of critical sections open now. An enter adds one and a leave removes one.
inline int simCriticalDepth = 0;


inline void System::CriticalSection(bool isEnable) {
	if (isEnable) {
		simCriticalDepth++;
		return;
	}

	if (simCriticalDepth == 0) {
		System::Abort("critical section left without a matching entry", __FILE__, __LINE__);
	}

	simCriticalDepth--;
}


// Set by a test around a call that runs as an interrupt would, so System::IsInterrupt() answers as the port does.
inline bool simInterruptContext = false;


inline bool System::IsInterrupt() {
	return simInterruptContext;
}


// The number of critical sections open now. Zero means none is left open.
inline int SimCriticalDepth() {
	return simCriticalDepth;
}


// Aborts when a critical section is still open. The tests call it where the depth must be zero.
inline void SimAssertNoCriticalSection() {
	if (simCriticalDepth != 0) {
		System::Abort("critical section still open", __FILE__, __LINE__);
	}
}


// The port's tick is a 64-bit count of milliseconds from the start: TickHandler adds one per call, the application calls it once per
// millisecond (an assumption of the port, not checked here), and msPerTick is 1 on every port. The shim has no SysTick, so the count is the wall clock in the same unit, from the first read. Each
// read also sets the low 32 bits that GetTick32() returns, so the two agree after any read.
inline volatile uint32 System::tickLow = 0;


inline uint64 System::GetTick() {
	static const std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
	std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now() - startTime;
	uint64 tick = static_cast<uint64>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
	tickLow = static_cast<uint32>(tick);
	return tick;
}


// The port computes GetMs() as the tick times msPerTick, which is 1, so it returns the tick itself.
inline uint64 System::GetMs() {
	return GetTick();
}


inline void System::Abort(const char* message, const char* file, uint32 line) {
	std::fprintf(stderr, "sim abort: %s (%s:%u)\n", message != nullptr ? message : "no message", file != nullptr ? file : "?", line);
	std::abort();
}


inline void System::CriticalError(const char* message, const char* file, uint32 line) {
	System::Abort(message, file, line);
}
