#pragma once

#include <System/System.h>
#include <cstdio>
#include <cstdlib>

// Host definitions of the System functions that the heap reaches: IsInterrupt() answers as a port does, and Abort()
// ends the run. A test sets simInterrupt around a call that runs as an interrupt would.
inline bool simInterrupt = false;

inline bool System::IsInterrupt() {
	return simInterrupt;
}

inline void System::Abort(const char* message, const char* file, uint32 line) {
	std::fprintf(stderr, "abort: %s (%s:%u)\n", message != nullptr ? message : "", file != nullptr ? file : "", static_cast<unsigned>(line));
	std::abort();
}

// Check helpers for the host tests. A failed check is printed and counted, and the test keeps going,
// so one run reports every failure. A crash ends the run with a non-zero exit code.
namespace MemorySim {

inline int failedChecks = 0;

inline void Check(bool condition, const char* expression, const char* file, int line) {
    if (condition) {
        return;
    }

    failedChecks++;
    std::fprintf(stderr, "check failed: %s (%s:%d)\n", expression, file, line);
}

// Prints the summary and returns the exit code for the test executable.
inline int Finish(const char* testName) {
    if (failedChecks == 0) {
        std::printf("%s: all checks passed\n", testName);
        return 0;
    }

    std::printf("%s: %d check(s) failed\n", testName, failedChecks);
    return 1;
}

}

#define SIM_CHECK(condition) MemorySim::Check((condition), #condition, __FILE__, __LINE__)
