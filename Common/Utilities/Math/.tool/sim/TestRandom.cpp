#include <Utilities/Math/MathUtilities.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>


// Host tests for MathUtilities::Random with integer types. Every value must lie in [a, b]. The fixed-point rejection is a
// compile-time check: a call with an IQ type does not compile (see the note at the end of this file).


namespace {


int failureCount = 0;


void ExpectInRange(int64_t value, int64_t low, int64_t high, int line) {
	if (value < low || value > high) {
		failureCount++;
		std::fprintf(stderr, "TestRandom.cpp:%d: value %lld outside [%lld, %lld]\n", line, static_cast<long long>(value),
			static_cast<long long>(low), static_cast<long long>(high));
	}
}


void IntegerCases() {
	std::srand(7);
	for (int index = 0; index < 1000; index++) {
		int32_t small = MathUtilities::Random<int32_t>(1, 10);
		ExpectInRange(small, 1, 10, __LINE__);

		uint8_t byte = MathUtilities::Random<uint8_t>(0, 3);
		ExpectInRange(byte, 0, 3, __LINE__);

		int16_t negative = MathUtilities::Random<int16_t>(-5, -2);
		ExpectInRange(negative, -5, -2, __LINE__);
	}
}


}


// Note for the owner: a fixed-point type does not compile here. The call
//     MathUtilities::Random(IQ<16>(1), IQ<16>(10));
// fails with "no matching function for call to 'Random(IQ<16>, IQ<16>)'", then "constraints not satisfied", and the reason
// "the expression 'is_integral_v<_Tp> [with _Tp = IQ<16, 256>]' evaluated to 'false'".


int main() {
	IntegerCases();

	if (failureCount != 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failureCount);
		return 1;
	}
	std::printf("TestRandom passed\n");
	return 0;
}
