// expect: IQ arithmetic takes an integer scalar
#include <Utilities/Math/IQMath/IQMath.h>


void Divide() {
	IQ<16> value = IQ<16>(2) / 2.7f;
	(void)value;
}
