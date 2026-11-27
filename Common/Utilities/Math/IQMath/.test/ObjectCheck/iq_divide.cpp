#include <Utilities/Math/IQMath/IQ.h>


volatile int32 sourceRaw = 0x18000;
volatile int32 resultRaw = 0;


int main() {
	IQ<16> a = IQ<16>::FromRaw(sourceRaw);
	IQ<16> b = IQ<16>::FromRaw(sourceRaw + 1);
	IQ<16> quotient = a / b;
	resultRaw = quotient.raw;
	return 0;
}
