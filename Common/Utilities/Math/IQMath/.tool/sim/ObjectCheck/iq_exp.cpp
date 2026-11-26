#include <Utilities/Math/IQMath/IQMath.h>


volatile int32 sourceRaw = 0x18000;
volatile int32 resultRaw = 0;


int main() {
	IQ<16> x = IQ<16>::FromRaw(sourceRaw);
	IQ<16> value = exp(x);
	resultRaw = value.raw;
	return 0;
}
