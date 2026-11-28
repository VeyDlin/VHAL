#include <Utilities/Math/IQMath/IQMath.h>
#include <VhalCheck.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>


// Host tests of IQ<16> against double: the product, the quotient, the square root and atan2 over sweeps of values, the floor and the
// ceiling of negative values, and the saturating conversions to the integer types and to another format.
//
// The values of a sweep are computed in double and rounded to IQ with FromFloat, the run-time form of the constructor: the constructor
// from a floating value is consteval. The reference is the double value of the IQ operand, so the rounding of the input is not counted.


namespace {


using Q16 = IQ<16>;


double ToDoubleValue(Q16 value) {
	return static_cast<double>(value.raw) / static_cast<double>(Q16::one);
}


void CheckMultiplyDoubleSweep() {
	double maxError = 0.0;
	for (int ia = -40; ia <= 40; ++ia) {
		for (int ib = -40; ib <= 40; ++ib) {
			Q16 a = Q16::FromFloat(static_cast<float>(ia * 0.37 + 0.011));
			Q16 b = Q16::FromFloat(static_cast<float>(ib * 0.53 - 0.007));
			Q16 product = a * b;
			double reference = ToDoubleValue(a) * ToDoubleValue(b);
			maxError = std::max(maxError, std::fabs(ToDoubleValue(product) - reference));
		}
	}
	VHAL_CHECK(maxError <= 2.0 / 65536.0);
}


void CheckDivideDoubleSweep() {
	double maxError = 0.0;
	for (int ia = -40; ia <= 40; ++ia) {
		for (int ib = -40; ib <= 40; ++ib) {
			Q16 a = Q16::FromFloat(static_cast<float>(ia * 0.37 + 0.011));
			Q16 b = Q16::FromFloat(static_cast<float>(ib * 0.53 + 0.31));
			if (std::fabs(ToDoubleValue(b)) < 0.2) {
				continue;
			}
			Q16 quotient = a / b;
			double reference = ToDoubleValue(a) / ToDoubleValue(b);
			maxError = std::max(maxError, std::fabs(ToDoubleValue(quotient) - reference));
		}
	}
	VHAL_CHECK(maxError <= 2.0 / 65536.0);
}


void CheckSqrtDoubleSweep() {
	double maxError = 0.0;
	for (int i = 0; i <= 270; ++i) {
		Q16 x = Q16::FromFloat(static_cast<float>(i * 0.37));
		Q16 root = sqrt(x);
		maxError = std::max(maxError, std::fabs(ToDoubleValue(root) - std::sqrt(ToDoubleValue(x))));
	}
	VHAL_CHECK(maxError <= 3.0 / 65536.0);
}


void CheckAtan2DoubleSweep() {
	double maxError = 0.0;
	for (int iy = -20; iy <= 20; ++iy) {
		for (int ix = -20; ix <= 20; ++ix) {
			if (ix == 0 && iy == 0) {
				continue;
			}
			Q16 y = Q16::FromFloat(static_cast<float>(iy * 0.173));
			Q16 x = Q16::FromFloat(static_cast<float>(ix * 0.211));
			double reference = std::atan2(ToDoubleValue(y), ToDoubleValue(x));
			maxError = std::max(maxError, std::fabs(ToDoubleValue(atan2(y, x)) - reference));
		}
	}
	VHAL_CHECK(maxError < 2e-3);
}


// The exponential over the top of the Q16 range, where the result is largest and must stay positive and within 1 % of the double.
void CheckExpTopOfRange() {
	for (int i = 0; i <= 1000; ++i) {
		Q16 x = Q16::FromFloat(static_cast<float>(9.0 + i * 0.0013));
		Q16 value = exp(x);
		double reference = std::exp(ToDoubleValue(x));
		bool ok = value.raw > 0 && std::fabs(ToDoubleValue(value) - reference) <= 0.01 * reference;
		VHAL_CHECK(ok);
		if (!ok) {
			return;
		}
	}
}


void CheckCeilFloorNegative() {
	Q16 value(-1.5);
	VHAL_CHECK(floor(value).ToFloat() == -2.0f);
	VHAL_CHECK(ceil(value).ToFloat() == -1.0f);
}


// The conversions to the integer types saturate to the limits of the target type.
void CheckToIntegerSaturates() {
	VHAL_CHECK(static_cast<int8>(Q16(200.0)) == 127);
	VHAL_CHECK(static_cast<int8>(Q16(-200.0)) == -128);
	VHAL_CHECK(static_cast<uint8>(Q16(300.0)) == 255);
	VHAL_CHECK(static_cast<uint32>(Q16(-1.0)) == 0);
	VHAL_CHECK(static_cast<int32>(Q16(-5.0)) == -5);
}


// Conversion to another format saturates when the value does not fit the target range.
void CheckFormatConversionSaturates() {
	IQ<30> high(IQ<8>::FromRaw(INT32_MAX));
	IQ<30> low(IQ<8>::FromRaw(INT32_MIN));
	IQ<24> exact(IQ<8>(1.5));
	VHAL_CHECK(high.raw == INT32_MAX);
	VHAL_CHECK(low.raw == INT32_MIN);
	VHAL_CHECK(exact.raw == 25165824);
}


}


int main() {
	CheckMultiplyDoubleSweep();
	CheckDivideDoubleSweep();
	CheckSqrtDoubleSweep();
	CheckAtan2DoubleSweep();
	CheckExpTopOfRange();
	CheckCeilFloorNegative();
	CheckToIntegerSaturates();
	CheckFormatConversionSaturates();
	return VhalCheck::Finish("TestIQMathSweeps");
}
