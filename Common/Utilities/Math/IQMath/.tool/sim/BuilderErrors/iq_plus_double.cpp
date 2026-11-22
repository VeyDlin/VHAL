// expect: IQ arithmetic takes an integer scalar
#include <Utilities/Math/IQMath/IQMath.h>


void Offset() {
	IQ<16> value = IQ<16>(2) + 2.7;
	(void)value;
}
