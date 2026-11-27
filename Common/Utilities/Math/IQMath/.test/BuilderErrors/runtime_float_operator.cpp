// expect: is not a constant expression
#include <Utilities/Math/IQMath/IQMath.h>


// A run-time float is not an operand of an IQ operator. The fix is IQ<16>::FromFloat(factor) on the right of the operator.
void Scale(float factor) {
	IQ<16> value(2);
	IQ<16> scaled = value * factor;
	(void)scaled;
}
