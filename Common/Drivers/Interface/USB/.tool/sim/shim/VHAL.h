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


inline uint64 System::GetMs() {
	static const std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
	std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now() - startTime;
	return static_cast<uint64>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}


inline uint64 System::GetTick() {
	return GetMs();
}


inline void System::Abort(const char* message, const char* file, uint32 line) {
	std::fprintf(stderr, "sim abort: %s (%s:%u)\n", message != nullptr ? message : "no message", file != nullptr ? file : "?", line);
	std::abort();
}


inline void System::CriticalError(const char* message, const char* file, uint32 line) {
	System::Abort(message, file, line);
}
