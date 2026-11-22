#include <Utilities/Math/IQMath/IQMath.h>
#include <cstdint>
#include <cstdio>
#include <limits>


// Host tests for construction of an IQ from an integer of any width and signedness. Each expected raw value is worked out
// by hand in the comment beside it: an integer n becomes n * 2^Q raw, and a value beyond the int32 raw range saturates.


namespace {


int failureCount = 0;


template <typename RawType>
void ExpectRaw(RawType actual, int64_t expected, int line) {
	if (static_cast<int64_t>(actual) != expected) {
		failureCount++;
		std::fprintf(stderr, "TestIntegerConstruction.cpp:%d: raw %lld, expected %lld\n", line, static_cast<long long>(actual),
			static_cast<long long>(expected));
	}
}


// Constant expressions: the constructor runs at compile time, so these hold without a run.
static_assert(IQ<16>(static_cast<int8_t>(-128)).raw == -8388608);
static_assert(IQ<16>(static_cast<uint64_t>(18446744073709551615ULL)).raw == 2147483647);
static_assert(IQ<30>(-2).raw == -2147483647 - 1);


void SignedCases() {
	// -128 * 65536 = -8388608, and 127 * 65536 = 8323072.
	ExpectRaw(IQ<16>(static_cast<int8_t>(-128)).raw, -8388608, __LINE__);
	ExpectRaw(IQ<16>(static_cast<int8_t>(127)).raw, 8323072, __LINE__);
	// -32768 * 65536 = -2147483648, exactly the most negative raw value: no saturation needed.
	ExpectRaw(IQ<16>(static_cast<int16_t>(-32768)).raw, -2147483647 - 1, __LINE__);
	// 32767 * 65536 = 2147418112, which fits in int32.
	ExpectRaw(IQ<16>(static_cast<int16_t>(32767)).raw, 2147418112, __LINE__);
	// -7 * 65536 = -458752 for long, the type that is int32 on ARM.
	ExpectRaw(IQ<16>(static_cast<long>(-7)).raw, -458752, __LINE__);
	// 5 * 65536 = 327680 for a plain int.
	ExpectRaw(IQ<16>(5).raw, 327680, __LINE__);
	// 1000 * 65536 = 65536000 for int64, which is long long here.
	ExpectRaw(IQ<16>(static_cast<int64_t>(1000)).raw, 65536000, __LINE__);
	// -3 * 2^24 = -50331648 for int64 at Q24 (the raw value is -3 * 16777216).
	ExpectRaw(IQ<24>(static_cast<int64_t>(-3)).raw, -50331648, __LINE__);
	// Q30: 1 * 2^30 = 1073741824, and -2 * 2^30 = -2147483648, the most negative raw value.
	ExpectRaw(IQ<30>(1).raw, 1073741824, __LINE__);
	ExpectRaw(IQ<30>(-2).raw, -2147483647 - 1, __LINE__);
	// Assignment from an integer goes through the same constructor.
	IQ<16> assigned = static_cast<int32_t>(-2);
	ExpectRaw(assigned.raw, -131072, __LINE__);
}


void UnsignedCases() {
	// 255 * 65536 = 16711680 for uint8.
	ExpectRaw(IQ<16>(static_cast<uint8_t>(255)).raw, 16711680, __LINE__);
	// 0 is exact.
	ExpectRaw(IQ<16>(static_cast<uint8_t>(0)).raw, 0, __LINE__);
	// 32767 * 65536 = 2147418112 for uint16, which fits.
	ExpectRaw(IQ<16>(static_cast<uint16_t>(32767)).raw, 2147418112, __LINE__);
	// 65535 * 65536 = 4294901760 is beyond the raw range: saturates to the maximum.
	ExpectRaw(IQ<16>(static_cast<uint16_t>(65535)).raw, 2147483647, __LINE__);
	// Q24 with a uint32 of 127: 127 * 16777216 = 2130706432, which fits.
	ExpectRaw(IQ<24>(static_cast<uint32_t>(127)).raw, 2130706432, __LINE__);
	// Q24 with a uint32 of 128: 128 * 16777216 = 2147483648 saturates to the maximum.
	ExpectRaw(IQ<24>(static_cast<uint32_t>(128)).raw, 2147483647, __LINE__);
	// uint64 of 1000 at Q16: 65536000.
	ExpectRaw(IQ<16>(static_cast<uint64_t>(1000)).raw, 65536000, __LINE__);
	// The largest uint64 is above INT64_MAX: it saturates to the maximum raw value.
	ExpectRaw(IQ<16>(static_cast<uint64_t>(18446744073709551615ULL)).raw, 2147483647, __LINE__);
	// 2^63 is the first value above INT64_MAX, so it saturates as well.
	ExpectRaw(IQ<16>(static_cast<uint64_t>(9223372036854775808ULL)).raw, 2147483647, __LINE__);
	// 2^63 - 1 is INT64_MAX itself, still beyond int32 at Q16: saturates.
	ExpectRaw(IQ<16>(static_cast<uint64_t>(9223372036854775807ULL)).raw, 2147483647, __LINE__);
	// A uint64 of 0 is exact.
	ExpectRaw(IQ<16>(static_cast<uint64_t>(0)).raw, 0, __LINE__);
}


void SaturationCases() {
	const int64_t maxRaw = std::numeric_limits<int32_t>::max();
	const int64_t minRaw = std::numeric_limits<int32_t>::min();
	// The top of int32 at Q16 is 2147483647 / 65536 = 32767.99998; a 32768 is beyond it: 32768 * 65536 = 2^31 saturates.
	ExpectRaw(IQ<16>(static_cast<int32_t>(32768)).raw, maxRaw, __LINE__);
	ExpectRaw(IQ<16>(static_cast<int32_t>(32767)).raw, 2147418112, __LINE__);
	// -32769 * 65536 is below the most negative raw value: saturates to the minimum.
	ExpectRaw(IQ<16>(static_cast<int32_t>(-32769)).raw, minRaw, __LINE__);
	// The int32 extremes saturate at both ends.
	ExpectRaw(IQ<16>(std::numeric_limits<int32_t>::max()).raw, maxRaw, __LINE__);
	ExpectRaw(IQ<16>(std::numeric_limits<int32_t>::min()).raw, minRaw, __LINE__);
	// The int64 extremes saturate at both ends.
	ExpectRaw(IQ<16>(std::numeric_limits<int64_t>::max()).raw, maxRaw, __LINE__);
	ExpectRaw(IQ<16>(std::numeric_limits<int64_t>::min()).raw, minRaw, __LINE__);
	// 2^40 at Q16 is far beyond the range: saturates. -2^40 saturates to the minimum.
	ExpectRaw(IQ<16>(static_cast<int64_t>(1) << 40).raw, maxRaw, __LINE__);
	ExpectRaw(IQ<16>(-(static_cast<int64_t>(1) << 40)).raw, minRaw, __LINE__);
	// Q30: 2 * 2^30 = 2^31 saturates; 3 * 2^30 saturates as well.
	ExpectRaw(IQ<30>(2).raw, maxRaw, __LINE__);
	ExpectRaw(IQ<30>(3).raw, maxRaw, __LINE__);
	// Q30: -3 * 2^30 is below the minimum raw value: saturates.
	ExpectRaw(IQ<30>(-3).raw, minRaw, __LINE__);
	// Q1: 1 * 2 = 2 raw. At Q1, 2^30 * 2 = 2^31 saturates, computed without overflowing the 64-bit scalar.
	ExpectRaw(IQ<1>(1).raw, 2, __LINE__);
	ExpectRaw(IQ<1>(static_cast<int64_t>(1073741824)).raw, maxRaw, __LINE__);
	// Small values of the narrow types still fit at Q30: -1 * 2^30 and 1 * 2^30 are the only values that matter here.
	ExpectRaw(IQ<30>(static_cast<int8_t>(-1)).raw, -1073741824, __LINE__);
	ExpectRaw(IQ<30>(static_cast<uint8_t>(1)).raw, 1073741824, __LINE__);
}


}


int main() {
	SignedCases();
	UnsignedCases();
	SaturationCases();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestIntegerConstruction: %d check(s) failed\n", failureCount);
		return 1;
	}
	std::printf("TestIntegerConstruction: all checks passed\n");
	return 0;
}
