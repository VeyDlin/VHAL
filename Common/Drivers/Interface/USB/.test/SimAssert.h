#pragma once
#include <VHAL.h>
#include <VhalCheck.h>


// Assertion helpers for the USB host tests, built on the check header of the frame. A failed check prints its location and expression
// and is counted. A test executable ends with SimTest::Finish(), which returns non-zero when any check failed, and aborts when a
// critical section is still open, so a section that the code under test leaves open fails every test that runs it.
namespace SimTest {


// The failed checks of the test program. It is the counter that VhalCheck::Finish reads, so a test that counts a failure of its own
// adds to the same total.
inline int& failureCount = VhalCheck::failCount;


inline void Expect(bool condition, const char* expression, const char* file, int line) {
	VhalCheck::ExpectTrue(condition, file, line, expression);
}


// Compares two integers or two enums by value. The expected value is printed without its expression.
template<typename TActual, typename TExpected>
void ExpectEqual(const TActual& actual, const TExpected& expected, const char* expression, const char* file, int line) {
	VhalCheck::ExpectEqual(file, line, expression, "expected value", actual, expected);
}


inline int Finish(const char* testName) {
	SimAssertNoCriticalSection();

	return VhalCheck::Finish(testName);
}


}


#define SIM_EXPECT(condition) SimTest::Expect(static_cast<bool>(condition), #condition, __FILE__, __LINE__)
#define SIM_EXPECT_EQ(actual, expected) SimTest::ExpectEqual((actual), (expected), #actual, __FILE__, __LINE__)
