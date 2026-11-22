// expect: IQ arithmetic takes an integer scalar
#include <Utilities/Math/IQMath/IQMath.h>


void Scale() {
	IQ<16> value(2);
	value *= 0.5;
}
