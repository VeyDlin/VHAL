#pragma once
// Desktop stand-in for the VHAL umbrella header, for the Trigger host tests. The real System declaration is kept. A critical
// section is a recursive mutex here, so a test thread and a stand-in for an interrupt exclude each other as they would on a
// port. The shim counts the enters, so a test can check how many sections a call takes.
#include <System/System.h>
#include <cstdio>
#include <cstdlib>
#include <mutex>


// The number of critical sections open now, on the thread that holds the mutex.
inline int simCriticalDepth = 0;

// Every enter since the start of the test, so a test can count the sections one call takes.
inline uint32 simCriticalEnters = 0;

inline std::recursive_mutex simCriticalMutex;


inline void System::CriticalSection(bool isEnable) {
	if (isEnable) {
		simCriticalMutex.lock();
		simCriticalDepth++;
		simCriticalEnters++;
		return;
	}

	if (simCriticalDepth == 0) {
		System::Abort("critical section left without a matching entry", __FILE__, __LINE__);
	}

	simCriticalDepth--;
	simCriticalMutex.unlock();
}


inline void System::Abort(const char* message, const char* file, uint32 line) {
	std::fprintf(stderr, "sim abort: %s (%s:%u)\n", message != nullptr ? message : "no message", file != nullptr ? file : "?", line);
	std::abort();
}


inline void System::CriticalError(const char* message, const char* file, uint32 line) {
	System::Abort(message, file, line);
}
