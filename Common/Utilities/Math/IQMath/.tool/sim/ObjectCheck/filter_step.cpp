#include <Utilities/Math/Filter/LowPassFilter.h>
#include <Utilities/Math/IQMath/IQ.h>


volatile int32 sourceRaw = 0x18000;
volatile int32 resultRaw = 0;


int main() {
	LowPassFilter<IQ<16>> filter(IQ<16>(10), IQ<16>(1));
	filter.Set(IQ<16>::FromRaw(sourceRaw));
	filter.Resolve();
	resultRaw = filter.Get().raw;
	return 0;
}
