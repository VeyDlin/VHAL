// expect: is not a constant expression
#include <Utilities/Math/IQMath/IQMath.h>


// A run-time float does not convert to an IQ. The fix is IQ<16>::FromFloat(sample).
void Load(float sample) {
	IQ<16> value = sample;
	(void)value;
}
