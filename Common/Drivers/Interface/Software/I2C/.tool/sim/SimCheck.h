#pragma once
#include <cstdio>
#include <string>


// Check macros for the host tests. A failed check prints its line and the values, and the run exits non-zero.
namespace SimCheck {


inline int failureCount = 0;


inline void Fail(const char *file, int line, const char *expression) {
	failureCount++;
	std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
}


inline void FailEqual(const char *file, int line, const char *expression, long long actual, long long expected) {
	failureCount++;
	std::fprintf(stderr, "%s:%d: %s is %lld, expected %lld\n", file, line, expression, actual, expected);
}


inline void FailText(const char *file, int line, const char *expression, const std::string &actual, const std::string &expected) {
	failureCount++;
	std::fprintf(stderr, "%s:%d: %s is \"%s\", expected \"%s\"\n", file, line, expression, actual.c_str(), expected.c_str());
}


inline int Finish(const char *name) {
	if (failureCount == 0) {
		std::printf("%s: all checks passed\n", name);
		return 0;
	}

	std::printf("%s: %d check(s) failed\n", name, failureCount);
	return 1;
}


}


#define SIM_CHECK(condition) \
	do { \
		if (!(condition)) { \
			SimCheck::Fail(__FILE__, __LINE__, #condition); \
		} \
	} while (0)


#define SIM_EQ(actual, expected) \
	do { \
		long long simActual = static_cast<long long>(actual); \
		long long simExpected = static_cast<long long>(expected); \
		if (simActual != simExpected) { \
			SimCheck::FailEqual(__FILE__, __LINE__, #actual, simActual, simExpected); \
		} \
	} while (0)


#define SIM_TEXT(actual, expected) \
	do { \
		std::string simText = (actual); \
		std::string simExpectedText = (expected); \
		if (simText != simExpectedText) { \
			SimCheck::FailText(__FILE__, __LINE__, #actual, simText, simExpectedText); \
		} \
	} while (0)
