#pragma once

#include <cstdio>

// Check helpers for the VSTD host tests. A failed check is printed and counted, and the test keeps going,
// so one run reports every failure. A crash ends the run with a non-zero exit code.
namespace VstdSim {

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

#define SIM_CHECK(condition) VstdSim::Check((condition), #condition, __FILE__, __LINE__)
