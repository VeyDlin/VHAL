#include <Utilities/Math/IQMath/IQMath.h>
#include <VhalCheck.h>
#include <cmath>
#include <cstdint>


// Host tests of the saturation of IQ<16> on the operators that the other IQ suites do not reach: the compound assignments, and the
// conversion from a double or a float that is out of range or not a number. Values out of the raw range saturate to the limit of the
// raw range, and a NaN converts to 0.


namespace {


using Q16 = IQ<16>;


void CheckFromDoubleSaturates() {
	Q16 positive(1e30);
	Q16 negative(-1e30);
	Q16 notANumber = Q16::FromFloat(std::nanf(""));
	Q16 floatPositive(40000.0f);
	Q16 floatNegative(-40000.0f);
	VHAL_CHECK(positive.raw == INT32_MAX);
	VHAL_CHECK(negative.raw == INT32_MIN);
	VHAL_CHECK(notANumber.raw == 0);
	VHAL_CHECK(floatPositive.raw == INT32_MAX);
	VHAL_CHECK(floatNegative.raw == INT32_MIN);
}


void CheckAddSubCompoundSaturates() {
	Q16 maximum = Q16::FromRaw(INT32_MAX);
	Q16 minimum = Q16::FromRaw(INT32_MIN);
	Q16 one = Q16::FromRaw(1);

	VHAL_CHECK((maximum + one).raw == INT32_MAX);
	VHAL_CHECK((minimum - one).raw == INT32_MIN);
	VHAL_CHECK((minimum + minimum).raw == INT32_MIN);
	VHAL_CHECK((maximum - minimum).raw == INT32_MAX);
	VHAL_CHECK((maximum + 1).raw == INT32_MAX);

	Q16 accumulator = maximum;
	accumulator += one;
	VHAL_CHECK(accumulator.raw == INT32_MAX);

	accumulator = minimum;
	accumulator -= one;
	VHAL_CHECK(accumulator.raw == INT32_MIN);
}


void CheckMultiplyCompoundSaturates() {
	Q16 big(20000.0);
	Q16 negativeBig(-20000.0);

	VHAL_CHECK((big * big).raw == INT32_MAX);
	VHAL_CHECK((negativeBig * big).raw == INT32_MIN);
	VHAL_CHECK((2 * big).raw == INT32_MAX);
	VHAL_CHECK((negativeBig * 2).raw == INT32_MIN);

	Q16 accumulator = big;
	accumulator *= big;
	VHAL_CHECK(accumulator.raw == INT32_MAX);
}


}


int main() {
	CheckFromDoubleSaturates();
	CheckAddSubCompoundSaturates();
	CheckMultiplyCompoundSaturates();
	return VhalCheck::Finish("TestIQSaturation");
}
