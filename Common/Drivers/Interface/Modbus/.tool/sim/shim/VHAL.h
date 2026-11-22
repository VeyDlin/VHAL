#pragma once
// Desktop stand-in for the VHAL umbrella header, for the Modbus host tests. The real System declaration is kept, so the shared
// headers compile unchanged. The clock is virtual: SimStep advances simNowUs by simStepUs on every read of the clock or the tick,
// and then calls simDue, which the fake UART uses to deliver the received bytes that have come due. While simDue runs (inside a
// receive callback), reads do not advance time, so the callback sees the time of its byte.
#include <System/System.h>
#include <cstdio>
#include <cstdlib>


// The simulated time in microseconds, and the step each read adds.
inline uint64 simNowUs = 0;
inline uint32 simStepUs = 1;

// True while a due event runs. Reads of time do not advance the clock then.
inline bool simInDue = false;

// Runs the due events after a step of time. The fake UART sets it while it is attached.
inline void (*simDue)() = nullptr;

// Runs once per step, after the due events, when a test wants another device to run while this one waits. A loopback test sets it
// to the server's Process, so a client that waits for a reply lets the server answer. The test guards it against reentry.
inline void (*simIdle)() = nullptr;

// The number of critical sections open now.
inline int simCriticalDepth = 0;


inline void SimStep() {
	if (simInDue) {
		return;
	}

	simNowUs += simStepUs;
	if (simDue != nullptr) {
		simDue();
	}

	if (simIdle != nullptr) {
		simIdle();
	}
}


// The clock the transport reads: microseconds, truncated to 32 bits as a port does.
inline uint32 SimClockUs() {
	SimStep();
	return static_cast<uint32>(simNowUs);
}


inline volatile uint32 System::tickLow = 0;


inline uint64 System::GetTick() {
	SimStep();
	return simNowUs / 1000;
}


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


inline void System::Abort(const char* message, const char* file, uint32 line) {
	std::fprintf(stderr, "sim abort: %s (%s:%u)\n", message != nullptr ? message : "no message", file != nullptr ? file : "?", line);
	std::abort();
}


inline void System::CriticalError(const char* message, const char* file, uint32 line) {
	System::Abort(message, file, line);
}
