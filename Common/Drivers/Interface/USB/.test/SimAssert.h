#pragma once
#include <VHAL.h>
#include <cstdio>
#include <type_traits>


// Tiny assertion helper for the host tests, with no framework. A failed check prints its location and expression and is
// counted. A test executable ends with SimTest::Finish(), which returns non-zero when any check failed, and aborts when a
// critical section is still open, so a section that the code under test leaves open fails every test that runs it.
namespace SimTest {


inline int failureCount = 0;


template<typename T>
long long ToInteger(const T& value) {
	if constexpr (std::is_enum_v<T>) {
		return static_cast<long long>(static_cast<std::underlying_type_t<T>>(value));
	} else if constexpr (std::is_integral_v<T>) {
		return static_cast<long long>(value);
	} else {
		static_assert(std::is_integral_v<T> || std::is_enum_v<T>, "SimTest compares integers and enums only");
		return 0;
	}
}


inline void Expect(bool condition, const char* expression, const char* file, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
	}
}


// Comparing through long long keeps signed and unsigned operands from warning under -Werror.
template<typename TActual, typename TExpected>
void ExpectEqual(const TActual& actual, const TExpected& expected, const char* expression, const char* file, int line) {
	long long actualValue = ToInteger(actual);
	long long expectedValue = ToInteger(expected);

	if (actualValue != expectedValue) {
		failureCount++;
		std::fprintf(stderr, "%s:%d: %s is %lld, expected %lld\n", file, line, expression, actualValue, expectedValue);
	}
}


inline int Finish(const char* testName) {
	SimAssertNoCriticalSection();

	if (failureCount == 0) {
		std::printf("%s: all checks passed\n", testName);
		return 0;
	}

	std::printf("%s: %d check(s) failed\n", testName, failureCount);
	return 1;
}


}


#define SIM_EXPECT(condition) SimTest::Expect(static_cast<bool>(condition), #condition, __FILE__, __LINE__)
#define SIM_EXPECT_EQ(actual, expected) SimTest::ExpectEqual((actual), (expected), #actual, __FILE__, __LINE__)
