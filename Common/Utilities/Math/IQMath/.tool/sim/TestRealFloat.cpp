#include <Utilities/Math/IQMath/IQMath.h>
#include <cstdio>


// Host tests for the float scalars of an IQ. A float constant folds in the constructor and in an operator on the right of an
// IQ. A run-time float reaches an IQ only through IQ::FromFloat or RealFrom, so the conversion is written at the call site.


namespace {


int failureCount = 0;


void ExpectTrue(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestRealFloat.cpp:%d: check failed: %s\n", line, expression);
	}
}


#define REAL_TRUE(condition) ExpectTrue((condition), #condition, __LINE__)


// A named float constant is a constant expression, so it converts to IQ in a constant context and at run time.
constexpr float kHalf = 0.5f;


// Constant expressions: the float folds into the IQ at compile time.
static_assert(IQ<16>(0.5f).raw == 32768);
static_assert(IQ<16>(0.5).raw == 32768);
static_assert((IQ<16>(3) * 0.5f).raw == 98304);
static_assert((IQ<16>(3) * kHalf).raw == 98304);
static_assert((IQ<16>(3) * 0.5).raw == 98304);
static_assert((IQ<16>(3) < 3.5f));
static_assert((IQ<16>(3) > kHalf));
static_assert((IQ<16>(3) / 2.0f).raw == 98304);


void TestFromFloatAtRunTime() {
	volatile float runTime = 0.25f;
	IQ<16> value = IQ<16>::FromFloat(runTime);
	REAL_TRUE(value.raw == 16384);

	volatile float negative = -1.5f;
	REAL_TRUE(IQ<16>::FromFloat(negative).raw == -98304);
}


void TestFromFloatSaturates() {
	volatile float large = 1.0e9f;
	REAL_TRUE(IQ<16>::FromFloat(large).raw == 2147483647);
}


void TestRealFromFloatAndIQ() {
	volatile float runTime = 0.75f;
	REAL_TRUE(RealFrom<float>(runTime) == 0.75f);
	REAL_TRUE(RealFrom<IQ<16>>(runTime).raw == 49152);
}


void TestCompoundAssignWithConstant() {
	IQ<16> value(2);
	value *= 0.5;
	REAL_TRUE(value.raw == 65536);

	value /= 2.0f;
	REAL_TRUE(value.raw == 32768);

	value += 0.5f;
	REAL_TRUE(value.raw == 65536);
}


void TestRunTimeComparison() {
	volatile float runTime = 0.5f;
	IQ<16> value(1);
	REAL_TRUE(value > IQ<16>::FromFloat(runTime));
	REAL_TRUE(value >= IQ<16>::FromFloat(runTime));
}


}


int main() {
	TestFromFloatAtRunTime();
	TestFromFloatSaturates();
	TestRealFromFloatAndIQ();
	TestCompoundAssignWithConstant();
	TestRunTimeComparison();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestRealFloat: %d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("TestRealFloat: all checks passed\n");
	return 0;
}
