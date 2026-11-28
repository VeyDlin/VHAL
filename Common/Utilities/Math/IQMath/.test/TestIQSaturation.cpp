#include <Utilities/Math/IQMath/IQMath.h>
#include <VhalCheck.h>
#include <cstdint>


// Host tests of the saturation of IQ<16> on the compound assignments (+=, -=, *=) and of the conversion from a float that is out
// of range. The other saturation cases (IQ with IQ, with integer scalars, NaN, out-of-range double, negation, abs) are in TestRounding.


namespace {


using Q16 = IQ<16>;


void CheckFromFloatSaturates() {
	Q16 floatPositive(40000.0f);
	Q16 floatNegative(-40000.0f);
	VHAL_CHECK(floatPositive.raw == INT32_MAX);
	VHAL_CHECK(floatNegative.raw == INT32_MIN);
}


void CheckAddSubCompoundSaturates() {
	Q16 maximum = Q16::FromRaw(INT32_MAX);
	Q16 minimum = Q16::FromRaw(INT32_MIN);
	Q16 one = Q16::FromRaw(1);

	Q16 accumulator = maximum;
	accumulator += one;
	VHAL_CHECK(accumulator.raw == INT32_MAX);

	accumulator = minimum;
	accumulator -= one;
	VHAL_CHECK(accumulator.raw == INT32_MIN);
}


void CheckMultiplyCompoundSaturates() {
	Q16 big(20000.0);

	Q16 accumulator = big;
	accumulator *= big;
	VHAL_CHECK(accumulator.raw == INT32_MAX);
}


}


int main() {
	CheckFromFloatSaturates();
	CheckAddSubCompoundSaturates();
	CheckMultiplyCompoundSaturates();
	return VhalCheck::Finish("TestIQSaturation");
}
