#include <VHAL.h>
#include <RT9467Registers.h>
#include <cstdio>

// Host tests for the float encoding of the RT9467 configuration constants. A value outside the range of a parameter is clamped
// to the range before it is converted to the register step, so a negative value encodes as the minimum, not as the maximum.


namespace {


int failureCount = 0;


void ExpectTrue(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestRT9467Encode.cpp:%d: check failed: %s\n", line, expression);
	}
}


#define RT9467_TRUE(condition) ExpectTrue((condition), #condition, __LINE__)


// Charge current: 100 mA to 5000 mA in 100 mA steps, so 2 A is step 19.
void TestChargeCurrentInRange() {
	RT9467_TRUE(RT9467Registers::Config::Ichg.Encode(0.1f) == 0);
	RT9467_TRUE(RT9467Registers::Config::Ichg.Encode(2.0f) == 19);
	RT9467_TRUE(RT9467Registers::Config::Ichg.Encode(5.0f) == 49);
}


void TestChargeCurrentAboveRangeIsClamped() {
	RT9467_TRUE(RT9467Registers::Config::Ichg.Encode(10.0f) == 49);
}


void TestChargeCurrentBelowRangeIsClamped() {
	RT9467_TRUE(RT9467Registers::Config::Ichg.Encode(0.0f) == 0);
	RT9467_TRUE(RT9467Registers::Config::Ichg.Encode(-1.0f) == 0);
	RT9467_TRUE(RT9467Registers::Config::Ichg.Encode(-1.0e6f) == 0);
}


// Charge voltage: 3.9 V to 4.71 V in 10 mV steps, so 4.2 V is step 30.
void TestChargeVoltageClamped() {
	RT9467_TRUE(RT9467Registers::Config::ChargeVolt.Encode(4.2f) == 30);
	RT9467_TRUE(RT9467Registers::Config::ChargeVolt.Encode(100.0f) == 81);
	RT9467_TRUE(RT9467Registers::Config::ChargeVolt.Encode(-4.2f) == 0);
}


}


int main() {
	TestChargeCurrentInRange();
	TestChargeCurrentAboveRangeIsClamped();
	TestChargeCurrentBelowRangeIsClamped();
	TestChargeVoltageClamped();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestRT9467Encode: %d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("TestRT9467Encode: all checks passed\n");
	return 0;
}
