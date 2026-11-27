#pragma once
#include <cstdio>


// Counts the failed checks of a test. main returns the count's result, so a failing check fails its test.
inline int simFailures = 0;


inline void SimExpectAt(bool condition, const char* text, int line) {
	if (!condition) {
		std::fprintf(stderr, "check failed at line %d: %s\n", line, text);
		simFailures++;
	}
}


#define SimCheck(condition) SimExpectAt((condition), #condition, __LINE__)
