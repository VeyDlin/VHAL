#pragma once
#include <cstdio>


// Counts the failed checks of one test program. SIM_EXPECT prints the text of the condition and its line when it fails.
inline int simFailures = 0;


inline void SimExpect(bool condition, const char* text, int line) {
	if (!condition) {
		std::printf("FAILED line %d: %s\n", line, text);
		simFailures++;
	}
}


#define SIM_EXPECT(condition) SimExpect((condition), #condition, __LINE__)


// Prints the outcome of the program and returns its exit code.
inline int SimFinish(const char* program) {
	if (simFailures == 0) {
		std::printf("%s: all checks passed\n", program);
		return 0;
	}

	std::printf("%s: %d check(s) failed\n", program, simFailures);
	return 1;
}
