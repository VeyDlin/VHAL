#include <VHAL.h>
#include <Stm32F4DacSim.h>
#include <Adapter/Port/STM32/F4/DACAdapterF4.h>
#include <cstdio>


// Host tests for the F4 DAC adapter: the trigger the channel starts with, and the enable that follows a configuration. The
// register model is in shim/Stm32F4DacSim.h.


namespace {


int failureCount = 0;

DAC_TypeDef regs;


void ExpectEqual(unsigned long long actual, unsigned long long expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestDacAdapterF4.cpp:%d: check failed: %s (actual %llu, expected %llu)\n", line, expression, actual, expected);
	}
}


#define EXPECT_EQ(actual, expected) ExpectEqual(static_cast<unsigned long long>(actual), static_cast<unsigned long long>(expected), #actual " == " #expected, __LINE__)


void ResetCase() {
	SimDacResetModel();
	regs = DAC_TypeDef();
}


// A channel with no trigger chosen starts on software, as before the trigger option was honoured.
void TestDefaultTriggerIsSoftware() {
	ResetCase();
	DACAdapterF4 adapter(&regs, 1);
	DACAdapterF4::Parameters params;

	adapter.SetParameters(params);

	EXPECT_EQ(simDacInitCount, 1);
	EXPECT_EQ(simDacLastTrigger, LL_DAC_TRIG_SOFTWARE);
}



// Timer 6 is the zero code of the register, so a channel that names it must still get it, not software.
void TestNamedTimer6TriggerIsNotSoftware() {
	ResetCase();
	DACAdapterF4 adapter(&regs, 1);
	DACAdapterF4::Parameters params;
	params.triggerSource = DACAdapterF4::TriggerSource::Timer6Trigger;

	adapter.SetParameters(params);

	EXPECT_EQ(simDacLastTrigger, LL_DAC_TRIG_EXT_TIM6_TRGO);
}



void TestNamedTimer2TriggerReachesRegister() {
	ResetCase();
	DACAdapterF4 adapter(&regs, 1);
	DACAdapterF4::Parameters params;
	params.triggerSource = DACAdapterF4::TriggerSource::Timer2Trigger;

	adapter.SetParameters(params);

	EXPECT_EQ(simDacLastTrigger, LL_DAC_TRIG_EXT_TIM2_TRGO);
}



// The channel is enabled only after the initialization succeeded. A refused configuration leaves it off.
void TestEnableSkippedAfterFailedInitialization() {
	ResetCase();
	DACAdapterF4 adapter(&regs, 1);
	adapter.beforePeripheryInit = []() {
		return ResultStatus::error;
	};
	DACAdapterF4::Parameters params;

	adapter.SetParameters(params);

	EXPECT_EQ(simDacInitCount, 0);
	EXPECT_EQ(simDacEnableCount, 0);
}


} // namespace


int main() {
	TestDefaultTriggerIsSoftware();
	TestNamedTimer6TriggerIsNotSoftware();
	TestNamedTimer2TriggerReachesRegister();
	TestEnableSkippedAfterFailedInitialization();

	if (failureCount != 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("all DAC checks passed\n");
	return 0;
}
