#include <Utilities/Math/IQMath/IQMath.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>


// Host tests for the IQ core: rounding of construction, multiplication and division, integer scalars, and saturation.
// Each expected raw value is worked out by hand in the comment beside it.


namespace {


int failureCount = 0;


template <typename RawType>
void ExpectRaw(RawType actual, int64_t expected, int line) {
	if (static_cast<int64_t>(actual) != expected) {
		failureCount++;
		std::fprintf(stderr, "TestRounding.cpp:%d: raw %lld, expected %lld\n", line, static_cast<long long>(actual),
			static_cast<long long>(expected));
	}
}


// Construction from float and double rounds to the nearest LSB, ties away from zero.
void ConstructionCases() {
	// 1.5 LSB at Q16 is 1.5 / 65536: a tie, rounded away from zero to 2 LSB. The mirror image rounds to -2.
	ExpectRaw(IQ<16>(1.5 / 65536.0).raw, 2, __LINE__);
	ExpectRaw(IQ<16>(-1.5 / 65536.0).raw, -2, __LINE__);
	// 2.5 LSB is a tie as well: 3 LSB, and -3 LSB.
	ExpectRaw(IQ<16>(2.5 / 65536.0).raw, 3, __LINE__);
	ExpectRaw(IQ<16>(-2.5 / 65536.0).raw, -3, __LINE__);
	// 1.4 LSB rounds down to 1 and 1.6 LSB rounds up to 2.
	ExpectRaw(IQ<16>(1.4 / 65536.0).raw, 1, __LINE__);
	ExpectRaw(IQ<16>(1.6 / 65536.0).raw, 2, __LINE__);
	ExpectRaw(IQ<16>(-1.4 / 65536.0).raw, -1, __LINE__);
	// 0.1 * 65536 = 6553.6, so 6554 for float and double alike (the float 0.1 is 0.100000001490116, still 6553.6 LSB).
	ExpectRaw(IQ<16>(0.1f).raw, 6554, __LINE__);
	ExpectRaw(IQ<16>(0.1).raw, 6554, __LINE__);
	// -0.1 * 65536 = -6553.6, so -6554. Truncation toward zero would have given -6553.
	ExpectRaw(IQ<16>(-0.1).raw, -6554, __LINE__);
	// Q24: half an LSB is 0.5 / 2^24, a tie: 1 LSB, and -1 LSB.
	ExpectRaw(IQ<24>(0.5 / 16777216.0).raw, 1, __LINE__);
	ExpectRaw(IQ<24>(-0.5 / 16777216.0).raw, -1, __LINE__);
	// 3 is exact: 3 * 65536 = 196608.
	ExpectRaw(IQ<16>(3.0).raw, 196608, __LINE__);
	ExpectRaw(IQ<16>(3).raw, 196608, __LINE__);
	// Saturation and NaN: beyond the range the raw value is the limit, NaN is 0.
	const int64_t maxRaw = std::numeric_limits<int32_t>::max();
	const int64_t minRaw = std::numeric_limits<int32_t>::min();
	ExpectRaw(IQ<16>(1e30).raw, maxRaw, __LINE__);
	ExpectRaw(IQ<16>(-1e30).raw, minRaw, __LINE__);
	ExpectRaw(IQ<16>(std::numeric_limits<double>::infinity()).raw, maxRaw, __LINE__);
	ExpectRaw(IQ<16>(-std::numeric_limits<double>::infinity()).raw, minRaw, __LINE__);
	ExpectRaw(IQ<16>(std::numeric_limits<double>::quiet_NaN()).raw, 0, __LINE__);
	ExpectRaw(IQ<16>(std::numeric_limits<float>::quiet_NaN()).raw, 0, __LINE__);
	// 2147483646.25 LSB is a value just below the top raw value: 0.25 LSB rounds down to 2147483646.
	ExpectRaw(IQ<16>(2147483646.25 / 65536.0).raw, 2147483646, __LINE__);
	// 2147483646.75 LSB rounds up to the top raw value.
	ExpectRaw(IQ<16>(2147483646.75 / 65536.0).raw, 2147483647, __LINE__);
}


// The reference for construction: the nearest integer of the scaled value, ties away from zero (llround), with the
// saturation of the raw range. The scaled value is exact in double for every input used here.
int64_t ReferenceRaw(double scaled) {
	if (scaled >= 2147483647.0) {
		return std::numeric_limits<int32_t>::max();
	}
	if (scaled <= -2147483648.0) {
		return std::numeric_limits<int32_t>::min();
	}
	return static_cast<int64_t>(std::llround(scaled));
}


// Random float and double inputs, for every Q. Half of them are k / 2^(Q+1) for a random integer k, which are exact ties
// (k odd) or exact halves; the rest are uniform floats. Each construction must match the reference.
template<int Q>
void RandomConstructionCases(int line) {
	std::mt19937 generator(12345u);
	std::uniform_real_distribution<float> uniform(-3.0f, 3.0f);
	std::uniform_int_distribution<int32_t> steps(-(1 << 20), 1 << 20);
	const double one = static_cast<double>(int64_t(1) << Q);
	const float tieScale = 1.0f / static_cast<float>(int64_t(1) << (Q + 1));

	for (int i = 0; i < 100000; i++) {
		float tie = static_cast<float>(steps(generator)) * tieScale;
		float uniformValue = uniform(generator);
		for (float value : {tie, uniformValue}) {
			int64_t expected = ReferenceRaw(static_cast<double>(value) * one);
			ExpectRaw(IQ<Q>(value).raw, expected, line);
			ExpectRaw(IQ<Q>(static_cast<double>(value)).raw, expected, line);
		}
	}
}


// IQ * IQ rounds to nearest. A tie goes toward +infinity: -1.5 LSB becomes -1, and 1.5 LSB becomes 2.
void MultiplyCases() {
	// 1.5 * 2 = 3 exactly.
	ExpectRaw((IQ<16>(1.5) * IQ<16>(2)).raw, 196608, __LINE__);
	// -1.5 * 2.25 = -3.375, exact: -3.375 * 65536 = -221184.
	ExpectRaw((IQ<16>(-1.5) * IQ<16>(2.25)).raw, -221184, __LINE__);
	// 3 LSB times 0.5 is 1.5 LSB: a tie, toward +infinity gives 2. The negative tie gives -1.
	ExpectRaw((IQ<16>::FromRaw(3) * IQ<16>::FromRaw(32768)).raw, 2, __LINE__);
	ExpectRaw((IQ<16>::FromRaw(-3) * IQ<16>::FromRaw(32768)).raw, -1, __LINE__);
	// -1 LSB times 0.5 is -0.5 LSB: toward +infinity gives 0.
	ExpectRaw((IQ<16>::FromRaw(-1) * IQ<16>::FromRaw(32768)).raw, 0, __LINE__);
	// 1 LSB times 0.49998 (raw 32767) is 0.49998 LSB: 0. Times 0.50002 (raw 32769): 1.
	ExpectRaw((IQ<16>::FromRaw(1) * IQ<16>::FromRaw(32767)).raw, 0, __LINE__);
	ExpectRaw((IQ<16>::FromRaw(1) * IQ<16>::FromRaw(32769)).raw, 1, __LINE__);
	// Q24: 5 LSB times 0.5 is 2.5 LSB: a tie, 3. The negative case -2.5 LSB gives -2.
	ExpectRaw((IQ<24>::FromRaw(5) * IQ<24>::FromRaw(8388608)).raw, 3, __LINE__);
	ExpectRaw((IQ<24>::FromRaw(-5) * IQ<24>::FromRaw(8388608)).raw, -2, __LINE__);
	// 0.1 is 6554 LSB, so the raw product is 6554 * 6554 = 42954916; divided by 65536 that is 655.44, so 655.
	ExpectRaw((IQ<16>(0.1) * IQ<16>(0.1)).raw, 655, __LINE__);
	// A float constant made explicit: IQ(2.7) is 176947 (2.7 * 65536 = 176947.2), and times IQ(2) is 353894.
	ExpectRaw((IQ<16>(2.7) * IQ<16>(2)).raw, 353894, __LINE__);
}


// IQ / IQ and IQ / integer round to nearest, ties away from zero.
void DivideCases() {
	// 1 / 3 = 21845.33 LSB: 21845. 2 / 3 = 43690.67 LSB: 43691.
	ExpectRaw((IQ<16>(1) / IQ<16>(3)).raw, 21845, __LINE__);
	ExpectRaw((IQ<16>(2) / IQ<16>(3)).raw, 43691, __LINE__);
	ExpectRaw((IQ<16>(-1) / IQ<16>(3)).raw, -21845, __LINE__);
	ExpectRaw((IQ<16>(-2) / IQ<16>(3)).raw, -43691, __LINE__);
	// Ties with raw operands: 1 LSB / 131072 (= 2.0) with 2^16 scaling is 0.5 LSB: a tie, away from zero gives 1.
	ExpectRaw((IQ<16>::FromRaw(1) / IQ<16>::FromRaw(131072)).raw, 1, __LINE__);
	ExpectRaw((IQ<16>::FromRaw(-1) / IQ<16>::FromRaw(131072)).raw, -1, __LINE__);
	ExpectRaw((IQ<16>::FromRaw(1) / IQ<16>::FromRaw(-131072)).raw, -1, __LINE__);
	// 3 LSB / 2.0 is 1.5 LSB: a tie, 2. The negative divisor flips the sign of the result.
	ExpectRaw((IQ<16>::FromRaw(3) / IQ<16>::FromRaw(131072)).raw, 2, __LINE__);
	ExpectRaw((IQ<16>::FromRaw(3) / IQ<16>::FromRaw(-131072)).raw, -2, __LINE__);
	// Q24: 1 / 3 = 5592405.33 LSB: 5592405. 2 / 3 = 11184810.67 LSB: 11184811.
	ExpectRaw((IQ<24>(1) / IQ<24>(3)).raw, 5592405, __LINE__);
	ExpectRaw((IQ<24>(2) / IQ<24>(3)).raw, 11184811, __LINE__);
	// Division by zero saturates by the sign of the numerator; 0 / 0 is 0.
	ExpectRaw((IQ<16>(3) / IQ<16>(0)).raw, 2147483647, __LINE__);
	ExpectRaw((IQ<16>(-3) / IQ<16>(0)).raw, -2147483648LL, __LINE__);
	ExpectRaw((IQ<16>(0) / IQ<16>(0)).raw, 0, __LINE__);
	// 30000 / 0.5 = 60000, beyond the Q16 range of 32768: saturates.
	ExpectRaw((IQ<16>(30000) / IQ<16>(0.5)).raw, 2147483647, __LINE__);
	ExpectRaw((IQ<16>(-30000) / IQ<16>(0.5)).raw, -2147483648LL, __LINE__);
	// Division by an integer: 5 LSB / 2 = 2.5 LSB: 3. -5 / 2 gives -3, 5 / -2 gives -3.
	ExpectRaw((IQ<16>::FromRaw(5) / 2).raw, 3, __LINE__);
	ExpectRaw((IQ<16>::FromRaw(-5) / 2).raw, -3, __LINE__);
	ExpectRaw((IQ<16>::FromRaw(5) / -2).raw, -3, __LINE__);
	// 6 / 4 = 1.5: 2. 7 / 4 = 1.75: 2. -6 / 4 = -1.5: -2.
	ExpectRaw((IQ<16>::FromRaw(6) / 4).raw, 2, __LINE__);
	ExpectRaw((IQ<16>::FromRaw(7) / 4).raw, 2, __LINE__);
	ExpectRaw((IQ<16>::FromRaw(-6) / 4).raw, -2, __LINE__);
	// 1 LSB / 2 = 0.5 LSB: 1; -1 LSB / 2: -1.
	ExpectRaw((IQ<16>::FromRaw(1) / 2).raw, 1, __LINE__);
	ExpectRaw((IQ<16>::FromRaw(-1) / 2).raw, -1, __LINE__);
	// 1 / 3 as an integer division: 65536 / 3 = 21845.33: 21845.
	ExpectRaw((IQ<16>(1) / 3).raw, 21845, __LINE__);
	// Division by zero with an integer divisor saturates; a divisor beyond int32 leaves a result of 0.
	ExpectRaw((IQ<16>(3) / 0).raw, 2147483647, __LINE__);
	ExpectRaw((IQ<16>(1) / static_cast<int64_t>(1000000000000000000LL)).raw, 0, __LINE__);
}


// Integer scalars are exact, and the compound forms agree with the binary ones.
void IntegerScalarCases() {
	// 3 * 2 = 6.0 raw 393216, and the same from the left.
	ExpectRaw((IQ<16>(3) * 2).raw, 393216, __LINE__);
	ExpectRaw((2 * IQ<16>(3)).raw, 393216, __LINE__);
	// 3 + 2 = 5.0 raw 327680; 2 - 3 = -1.0 raw -65536; 3 - 2 = 1.0 raw 65536.
	ExpectRaw((IQ<16>(3) + 2).raw, 327680, __LINE__);
	ExpectRaw((2 - IQ<16>(3)).raw, -65536, __LINE__);
	ExpectRaw((IQ<16>(3) - 2).raw, 65536, __LINE__);
	ExpectRaw((2 + IQ<16>(3)).raw, 327680, __LINE__);
	// 3 / 2 = 1.5 raw 98304.
	ExpectRaw((IQ<16>(3) / 2).raw, 98304, __LINE__);
	// Compound forms with an integer: 3 += 2 gives 5, 3 -= 2 gives 1, 3 *= 2 gives 6, 3 /= 2 gives 1.5.
	IQ<16> value = IQ<16>(3);
	value += 2;
	ExpectRaw(value.raw, 327680, __LINE__);
	value = IQ<16>(3);
	value -= 2;
	ExpectRaw(value.raw, 65536, __LINE__);
	value = IQ<16>(3);
	value *= 2;
	ExpectRaw(value.raw, 393216, __LINE__);
	value = IQ<16>(3);
	value /= 2;
	ExpectRaw(value.raw, 98304, __LINE__);
	// Unsigned scalars: 3 * 200 = 600, raw 39321600.
	ExpectRaw((IQ<16>(3) * static_cast<uint8_t>(200)).raw, 39321600, __LINE__);
	// The integer scalar 0 is exact; a negative scalar works with the 64-bit path.
	ExpectRaw((IQ<16>(3) * static_cast<int64_t>(-2)).raw, -393216, __LINE__);
}


// Saturation is unchanged for IQ with IQ, with int32 and with 64-bit integer scalars.
void SaturationCases() {
	const int64_t maxRaw = std::numeric_limits<int32_t>::max();
	const int64_t minRaw = std::numeric_limits<int32_t>::min();
	// Q16 holds up to 32767.99998.
	ExpectRaw((IQ<16>(30000) + IQ<16>(30000)).raw, maxRaw, __LINE__);
	ExpectRaw((IQ<16>(-30000) - IQ<16>(30000)).raw, minRaw, __LINE__);
	ExpectRaw((IQ<16>(20000) * IQ<16>(2)).raw, maxRaw, __LINE__);
	ExpectRaw((IQ<16>(30000) * 3).raw, maxRaw, __LINE__);
	ExpectRaw((IQ<16>(-30000) * 3).raw, minRaw, __LINE__);
	ExpectRaw((IQ<16>(30000) + 1000000).raw, maxRaw, __LINE__);
	ExpectRaw((2 * IQ<16>(20000)).raw, maxRaw, __LINE__);
	// The most negative raw value: negation and abs saturate to the maximum.
	ExpectRaw((-IQ<16>::FromRaw(-2147483647 - 1)).raw, maxRaw, __LINE__);
	ExpectRaw(abs(IQ<16>::FromRaw(-2147483647 - 1)).raw, maxRaw, __LINE__);
	// Integer scalars beyond int32 saturate, including the unsigned values above INT64_MAX.
	ExpectRaw((IQ<16>(1) * static_cast<uint64_t>(18446744073709551615ULL)).raw, maxRaw, __LINE__);
	ExpectRaw((IQ<16>(-1) * static_cast<uint64_t>(18446744073709551615ULL)).raw, minRaw, __LINE__);
	ExpectRaw((IQ<16>(0) * static_cast<uint64_t>(18446744073709551615ULL)).raw, 0, __LINE__);
	ExpectRaw((IQ<16>(1) + static_cast<uint64_t>(18446744073709551615ULL)).raw, maxRaw, __LINE__);
	ExpectRaw((IQ<16>(1) + static_cast<int64_t>(-5000000000LL)).raw, minRaw, __LINE__);
	ExpectRaw((IQ<16>(-1) - static_cast<int64_t>(9223372036854775807LL)).raw, minRaw, __LINE__);
	ExpectRaw((IQ<16>(1) * static_cast<int64_t>(4611686018427387904LL)).raw, maxRaw, __LINE__);
	ExpectRaw((IQ<16>(-1) * static_cast<int64_t>(4611686018427387904LL)).raw, minRaw, __LINE__);
	// Q24 holds up to 127.99999994: 100 + 100 and 100 * 2 saturate.
	ExpectRaw((IQ<24>(100) + IQ<24>(100)).raw, maxRaw, __LINE__);
	ExpectRaw((IQ<24>(100) * 2).raw, maxRaw, __LINE__);
}


}


int main() {
	ConstructionCases();
	RandomConstructionCases<8>(__LINE__);
	RandomConstructionCases<16>(__LINE__);
	RandomConstructionCases<24>(__LINE__);
	RandomConstructionCases<30>(__LINE__);
	MultiplyCases();
	DivideCases();
	IntegerScalarCases();
	SaturationCases();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestRounding: %d check(s) failed\n", failureCount);
		return 1;
	}
	std::printf("TestRounding: all checks passed\n");
	return 0;
}
