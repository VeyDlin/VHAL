#include <Utilities/DataTypes.h>
#include <Utilities/Math/MathUtilities.h>
#include <VhalCheck.h>
#include <cstdint>
#include <limits>


// Host tests of MathUtilities: SetMax with a negative value into an unsigned variable, an interpolation whose x0 is above x1, an
// integer interpolation whose product overflows int, and linear interpolation and extrapolation at the extremes of the integer types.


namespace {


// Exact y0 + (x2 - x0) * (y1 - y0) / (x1 - x0), truncated toward zero, saturated to the range of T.
// Each difference of 64-bit values is below 2^64 in magnitude, so the product fits an unsigned __int128.
template <typename T>
T ReferenceLinear(T x0, T y0, T x1, T y1, T x2) {
	__int128 dxSigned = static_cast<__int128>(x2) - x0;
	__int128 dySigned = static_cast<__int128>(y1) - y0;
	__int128 ddSigned = static_cast<__int128>(x1) - x0;
	unsigned __int128 dxAbs = static_cast<unsigned __int128>(dxSigned < 0 ? -dxSigned : dxSigned);
	unsigned __int128 dyAbs = static_cast<unsigned __int128>(dySigned < 0 ? -dySigned : dySigned);
	unsigned __int128 ddAbs = static_cast<unsigned __int128>(ddSigned < 0 ? -ddSigned : ddSigned);
	bool negative = ((dxSigned < 0) != (dySigned < 0)) != (ddSigned < 0);

	unsigned __int128 quotient = (dxAbs * dyAbs) / ddAbs;
	unsigned __int128 bound = static_cast<unsigned __int128>(1) << 65;
	if (quotient > bound) {
		quotient = bound;
	}

	__int128 step = negative ? -static_cast<__int128>(quotient) : static_cast<__int128>(quotient);
	__int128 value = static_cast<__int128>(y0) + step;
	__int128 lowest = static_cast<__int128>(std::numeric_limits<T>::min());
	__int128 highest = static_cast<__int128>(std::numeric_limits<T>::max());
	if (value < lowest) {
		value = lowest;
	}
	if (value > highest) {
		value = highest;
	}
	return static_cast<T>(value);
}


template <typename T>
void CheckLinearCase(T x0, T y0, T x1, T y1, T x2) {
	T expected = ReferenceLinear<T>(x0, y0, x1, y1, x2);
	T interpolated = MathUtilities::Interpolation(x0, y0, x1, y1, x2);
	T extrapolated = MathUtilities::Extrapolation(x0, y0, x1, y1, x2);
	VHAL_CHECK_EQ(interpolated, expected);
	VHAL_CHECK_EQ(extrapolated, expected);
}


void CheckSetMaxNegativeIntoUnsigned() {
	uint32 data = 5;
	MathUtilities::SetMax(data, -1);
	VHAL_CHECK_EQ(data, 5);
}


void CheckInterpolationReversedRange() {
	float value = MathUtilities::Interpolation(4.0f, 18.0f, 0.0f, 10.0f, 2.0f);
	VHAL_CHECK_NEAR(value, 14.0, 1e-5);
}


void CheckInterpolationIntOverflow() {
	int value = MathUtilities::Interpolation(0, 0, 100000, 100000, 50000);
	VHAL_CHECK_EQ(value, 50000);
}


void CheckLinearInt32Extremes() {
	CheckLinearCase<int32_t>(INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX, 0);
	CheckLinearCase<int32_t>(INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX, INT32_MAX);
	CheckLinearCase<int32_t>(INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN, INT32_MIN);
	CheckLinearCase<int32_t>(-2, 5, 3, -7, 1);
	CheckLinearCase<int32_t>(0, 0, 1, INT32_MAX, 2);
	CheckLinearCase<int32_t>(0, 0, 1, INT32_MIN, 2);
}


void CheckLinearUint32Extremes() {
	CheckLinearCase<uint32_t>(0u, 0u, UINT32_MAX, UINT32_MAX, UINT32_MAX / 2);
	CheckLinearCase<uint32_t>(UINT32_MAX, UINT32_MAX, 0u, 0u, 0u);
	CheckLinearCase<uint32_t>(UINT32_MAX, 0u, 0u, UINT32_MAX, 1u);
	CheckLinearCase<uint32_t>(0u, UINT32_MAX, UINT32_MAX, 0u, UINT32_MAX);
	CheckLinearCase<uint32_t>(0u, 0u, 1u, UINT32_MAX, 2u);
}


void CheckLinearInt64Extremes() {
	CheckLinearCase<int64_t>(INT64_MIN, 0, INT64_MAX, INT64_MAX, 0);
	CheckLinearCase<int64_t>(INT64_MIN, INT64_MIN, INT64_MAX, INT64_MAX, INT64_MAX);
	CheckLinearCase<int64_t>(INT64_MAX, 0, INT64_MIN, INT64_MIN, 0);
	CheckLinearCase<int64_t>(0, 0, 1, INT64_MAX, 2);
}


void CheckLinearUint64Extremes() {
	CheckLinearCase<uint64_t>(0u, 0u, UINT64_MAX, UINT64_MAX, UINT64_MAX - 1);
	CheckLinearCase<uint64_t>(UINT64_MAX, UINT64_MAX, 0u, 0u, 0u);
	CheckLinearCase<uint64_t>(UINT64_MAX, 0u, 0u, UINT64_MAX, 1u);
	CheckLinearCase<uint64_t>(1u, 5u, 3u, 7u, 2u);
	CheckLinearCase<uint64_t>(0u, 0u, 1u, UINT64_MAX, 2u);
}


}


int main() {
	CheckSetMaxNegativeIntoUnsigned();
	CheckInterpolationReversedRange();
	CheckInterpolationIntOverflow();
	CheckLinearInt32Extremes();
	CheckLinearUint32Extremes();
	CheckLinearInt64Extremes();
	CheckLinearUint64Extremes();
	return VhalCheck::Finish("TestMathExtremes");
}
