#include <VHAL.h>
#include <Stm32F4AdcSim.h>
#include <Adapter/Port/STM32/F4/ADCAdapterF4.h>
#include <cstdio>


// Host tests for the F4 ADC adapter: the size a read must have to hold one result. The register model is in
// shim/Stm32F4AdcSim.h.


namespace {


int failureCount = 0;

ADC_TypeDef regs;


void Expect(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestAdcAdapterF4.cpp:%d: check failed: %s\n", line, expression);
	}
}


#define EXPECT(condition) Expect((condition), #condition, __LINE__)


void ResetCase() {
	regs = ADC_TypeDef();
}


// The blocking read already refuses a size that holds no result. It is the reference for the asynchronous read.
void TestBlockingReadRefusesSizeBelowOneResult() {
	ResetCase();
	ADCAdapterF4 adapter(&regs);
	uint8 buffer[1] = { 0 };

	EXPECT(adapter.ReadArray<uint8>(buffer, 1) == ResultStatus::invalidParameter);
}



// At 12-bit resolution one result takes two bytes. A one-byte buffer holds no result, so the read is refused before a
// conversion can write past the buffer.
void TestAsyncReadRefusesSizeBelowOneResult() {
	ResetCase();
	ADCAdapterF4 adapter(&regs);
	uint8 buffer[2] = { 0, 0 };

	EXPECT(adapter.ReadArrayAsync<uint8>(buffer, 1) == ResultStatus::invalidParameter);
	EXPECT(adapter.ReadArrayAsync<uint8>(buffer, 2) == ResultStatus::ok);
}


} // namespace


int main() {
	TestBlockingReadRefusesSizeBelowOneResult();
	TestAsyncReadRefusesSizeBelowOneResult();

	if (failureCount != 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("all ADC checks passed\n");
	return 0;
}
