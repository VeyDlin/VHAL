// expect: no match for
#include <Utilities/Math/IQMath/IQMath.h>


void Scale() {
	IQ<16> value = 2.7f * IQ<16>(2);
	(void)value;
}
