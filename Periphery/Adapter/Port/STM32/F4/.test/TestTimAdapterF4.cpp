#include <VHAL.h>
#include <Stm32F4TimSim.h>
#include <Adapter/Port/STM32/F4/TIMAdapterF4.h>
#include <cstdio>
#include <vector>


// Host tests for the F4 timer adapter: the input capture configuration in the CCMR, CCER and OR registers of each channel,
// the clock division, and the capture and output events of channels 2 and 4 on their own flags. The register model is in
// shim/Stm32F4TimSim.h.


namespace {


int failureCount = 0;

TIM_TypeDef regs;
std::vector<uint8> inputEvents;
std::vector<uint8> outputEvents;


void Expect(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestTimAdapterF4.cpp:%d: check failed: %s\n", line, expression);
	}
}


void ExpectEqual(unsigned long long actual, unsigned long long expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestTimAdapterF4.cpp:%d: check failed: %s (actual %llu, expected %llu)\n", line, expression, actual, expected);
	}
}


#define EXPECT(condition) Expect((condition), #condition, __LINE__)
#define EXPECT_EQ(actual, expected) ExpectEqual(static_cast<unsigned long long>(actual), static_cast<unsigned long long>(expected), #actual " == " #expected, __LINE__)


void ResetCase() {
	regs = TIM_TypeDef();
	inputEvents.clear();
	outputEvents.clear();
}


void WireEvents(TIMAdapterF4 &adapter) {
	adapter.onInputCaptureEvent = [](uint8 channel) {
		inputEvents.push_back(channel);
	};
	adapter.onOutputCompareEvent = [](uint8 channel) {
		outputEvents.push_back(channel);
	};
}


// One channel's input capture parameters. The prescaler and the filter are raw LL values, as the G4 adapter takes them.
TIMAdapterF4::InputCaptureParameters Capture(
	TIMAdapterF4::ChannelOption channel,
	uint32 prescaler,
	uint32 filter,
	TIMAdapterF4::InputPolarity polarity,
	TIMAdapterF4::InputSelection selection
) {
	TIMAdapterF4::InputCaptureParameters params;
	params.channel = channel;
	params.prescaler = TIMAdapterF4::InputPrescalerOption(prescaler);
	params.filter = TIMAdapterF4::InputFilterOption(filter);
	params.remapping = TIMAdapterF4::InputRemappingOption(0);
	params.polarity = polarity;
	params.selection = selection;
	return params;
}


void ConfigureCapture(TIMAdapterF4 &adapter, TIMAdapterF4::InputCaptureParameters params) {
	EXPECT(adapter.ConfigInputCaptureParameters({ params }) == ResultStatus::ok);
}


// Channel 1 is set to direct input, rising edge, no prescaler and no filter: the register values of a plain capture.
void TestChannel1DirectRisingWritesCcmr1() {
	ResetCase();
	TIMAdapterF4 adapter(&regs, 84000000U);

	ConfigureCapture(adapter, Capture(
		TIMAdapterF4::Channel::C1,
		LL_TIM_ICPSC_DIV1,
		LL_TIM_IC_FILTER_FDIV1,
		TIMAdapterF4::InputPolarity::Rising,
		TIMAdapterF4::InputSelection::Direct
	));

	EXPECT_EQ(regs.CCMR1 & TIM_CCMR1_CC1S, TIM_CCMR1_CC1S_0);
	EXPECT_EQ(regs.CCMR1 & TIM_CCMR1_IC1PSC, 0);
	EXPECT_EQ(regs.CCMR1 & TIM_CCMR1_IC1F, 0);
	EXPECT_EQ(regs.CCER & (TIM_CCER_CC1P | TIM_CCER_CC1NP), 0);
}



// The prescaler and the filter of channel 1 are written to its byte of CCMR1, and the select of indirect and TRC inputs.
void TestChannel1PrescalerFilterAndSelection() {
	ResetCase();
	TIMAdapterF4 adapter(&regs, 84000000U);

	ConfigureCapture(adapter, Capture(
		TIMAdapterF4::Channel::C1,
		LL_TIM_ICPSC_DIV4,
		LL_TIM_IC_FILTER_FDIV4_N8,
		TIMAdapterF4::InputPolarity::Rising,
		TIMAdapterF4::InputSelection::Direct
	));
	EXPECT_EQ(regs.CCMR1 & TIM_CCMR1_IC1PSC, (LL_TIM_ICPSC_DIV4 >> 16U));
	EXPECT_EQ(regs.CCMR1 & TIM_CCMR1_IC1F, (LL_TIM_IC_FILTER_FDIV4_N8 >> 16U));

	ConfigureCapture(adapter, Capture(
		TIMAdapterF4::Channel::C1,
		LL_TIM_ICPSC_DIV1,
		LL_TIM_IC_FILTER_FDIV1,
		TIMAdapterF4::InputPolarity::Rising,
		TIMAdapterF4::InputSelection::Indirect
	));
	EXPECT_EQ(regs.CCMR1 & TIM_CCMR1_CC1S, TIM_CCMR1_CC1S_1);

	ConfigureCapture(adapter, Capture(
		TIMAdapterF4::Channel::C1,
		LL_TIM_ICPSC_DIV1,
		LL_TIM_IC_FILTER_FDIV1,
		TIMAdapterF4::InputPolarity::Rising,
		TIMAdapterF4::InputSelection::TRC
	));
	EXPECT_EQ(regs.CCMR1 & TIM_CCMR1_CC1S, TIM_CCMR1_CC1S);
}



// The polarity of each channel is its own pair of CCER bits: CC1P and CC1NP for channel 1, CC2P and CC2NP for channel 2, and so on.
void TestPolarityBitsPerChannel() {
	ResetCase();
	TIMAdapterF4 adapter(&regs, 84000000U);

	ConfigureCapture(adapter, Capture(
		TIMAdapterF4::Channel::C1,
		LL_TIM_ICPSC_DIV1,
		LL_TIM_IC_FILTER_FDIV1,
		TIMAdapterF4::InputPolarity::RisingFalling,
		TIMAdapterF4::InputSelection::Direct
	));
	EXPECT_EQ(regs.CCER & (TIM_CCER_CC1P | TIM_CCER_CC1NP), (TIM_CCER_CC1P | TIM_CCER_CC1NP));

	ConfigureCapture(adapter, Capture(
		TIMAdapterF4::Channel::C2,
		LL_TIM_ICPSC_DIV1,
		LL_TIM_IC_FILTER_FDIV1,
		TIMAdapterF4::InputPolarity::Falling,
		TIMAdapterF4::InputSelection::Direct
	));
	EXPECT_EQ(regs.CCER & (TIM_CCER_CC2P | TIM_CCER_CC2NP), TIM_CCER_CC2P);
	EXPECT_EQ(regs.CCER & (TIM_CCER_CC1P | TIM_CCER_CC1NP), (TIM_CCER_CC1P | TIM_CCER_CC1NP));

	ConfigureCapture(adapter, Capture(
		TIMAdapterF4::Channel::C3,
		LL_TIM_ICPSC_DIV1,
		LL_TIM_IC_FILTER_FDIV1,
		TIMAdapterF4::InputPolarity::RisingFalling,
		TIMAdapterF4::InputSelection::Direct
	));
	EXPECT_EQ(regs.CCER & (TIM_CCER_CC3P | TIM_CCER_CC3NP), (TIM_CCER_CC3P | TIM_CCER_CC3NP));

	ConfigureCapture(adapter, Capture(
		TIMAdapterF4::Channel::C4,
		LL_TIM_ICPSC_DIV1,
		LL_TIM_IC_FILTER_FDIV1,
		TIMAdapterF4::InputPolarity::Falling,
		TIMAdapterF4::InputSelection::Direct
	));
	EXPECT_EQ(regs.CCER & (TIM_CCER_CC4P | TIM_CCER_CC4NP), TIM_CCER_CC4P);

	ConfigureCapture(adapter, Capture(
		TIMAdapterF4::Channel::C1,
		LL_TIM_ICPSC_DIV1,
		LL_TIM_IC_FILTER_FDIV1,
		TIMAdapterF4::InputPolarity::Rising,
		TIMAdapterF4::InputSelection::Direct
	));
	EXPECT_EQ(regs.CCER & (TIM_CCER_CC1P | TIM_CCER_CC1NP), 0);
	EXPECT_EQ(regs.CCER & (TIM_CCER_CC2P | TIM_CCER_CC2NP), TIM_CCER_CC2P);
}



// Channel 2 has its select, prescaler and filter in the high byte of CCMR1, and channel 1 keeps its own bits.
void TestChannel2UsesHighByteOfCcmr1() {
	ResetCase();
	TIMAdapterF4 adapter(&regs, 84000000U);

	ConfigureCapture(adapter, Capture(
		TIMAdapterF4::Channel::C1,
		LL_TIM_ICPSC_DIV1,
		LL_TIM_IC_FILTER_FDIV1,
		TIMAdapterF4::InputPolarity::Rising,
		TIMAdapterF4::InputSelection::Direct
	));
	ConfigureCapture(adapter, Capture(
		TIMAdapterF4::Channel::C2,
		LL_TIM_ICPSC_DIV2,
		LL_TIM_IC_FILTER_FDIV4_N8,
		TIMAdapterF4::InputPolarity::Rising,
		TIMAdapterF4::InputSelection::Indirect
	));

	EXPECT_EQ(regs.CCMR1 & TIM_CCMR1_CC1S, TIM_CCMR1_CC1S_0);
	EXPECT_EQ((regs.CCMR1 & TIM_CCMR1_CC2S), (TIM_CCMR1_CC1S_1 << 8U));
	EXPECT_EQ((regs.CCMR1 & TIM_CCMR1_IC2PSC), ((LL_TIM_ICPSC_DIV2 >> 16U) << 8U));
	EXPECT_EQ((regs.CCMR1 & TIM_CCMR1_IC2F), ((LL_TIM_IC_FILTER_FDIV4_N8 >> 16U) << 8U));
	EXPECT_EQ(regs.CCMR2, 0);
}



// Channels 3 and 4 are in CCMR2, with the same layout as channels 1 and 2 in CCMR1.
void TestChannels3And4UseCcmr2() {
	ResetCase();
	TIMAdapterF4 adapter(&regs, 84000000U);

	ConfigureCapture(adapter, Capture(
		TIMAdapterF4::Channel::C3,
		LL_TIM_ICPSC_DIV1,
		LL_TIM_IC_FILTER_FDIV1,
		TIMAdapterF4::InputPolarity::Rising,
		TIMAdapterF4::InputSelection::TRC
	));
	ConfigureCapture(adapter, Capture(
		TIMAdapterF4::Channel::C4,
		LL_TIM_ICPSC_DIV8,
		LL_TIM_IC_FILTER_FDIV1,
		TIMAdapterF4::InputPolarity::Rising,
		TIMAdapterF4::InputSelection::Direct
	));

	EXPECT_EQ(regs.CCMR2 & TIM_CCMR2_CC3S, TIM_CCMR1_CC1S);
	EXPECT_EQ(regs.CCMR2 & TIM_CCMR2_CC4S, (TIM_CCMR1_CC1S_0 << 8U));
	EXPECT_EQ((regs.CCMR2 & TIM_CCMR2_IC4PSC), ((LL_TIM_ICPSC_DIV8 >> 16U) << 8U));
	EXPECT_EQ(regs.CCMR1, 0);
}



// A remapping of zero is the default. It writes nothing, so a value already in OR stays.
void TestZeroRemappingLeavesOrAlone() {
	ResetCase();
	TIMAdapterF4 adapter(&regs, 84000000U);
	regs.OR = 0x3;

	ConfigureCapture(adapter, Capture(
		TIMAdapterF4::Channel::C1,
		LL_TIM_ICPSC_DIV1,
		LL_TIM_IC_FILTER_FDIV1,
		TIMAdapterF4::InputPolarity::Rising,
		TIMAdapterF4::InputSelection::Direct
	));

	EXPECT_EQ(regs.OR, 0x3);
}



// A non-zero remapping is written to OR.
void TestRemappingIsWrittenToOr() {
	ResetCase();
	TIMAdapterF4 adapter(&regs, 84000000U);
	TIMAdapterF4::InputCaptureParameters params = Capture(
		TIMAdapterF4::Channel::C1,
		LL_TIM_ICPSC_DIV1,
		LL_TIM_IC_FILTER_FDIV1,
		TIMAdapterF4::InputPolarity::Rising,
		TIMAdapterF4::InputSelection::Direct
	);
	params.remapping = TIMAdapterF4::InputRemappingOption(0x2);

	ConfigureCapture(adapter, params);

	EXPECT_EQ(regs.OR, 0x2);
}



// Channel 2 as an input: its flag is an input event, and the channel 1 flag is not channel 2's event.
void TestCapture2EventFollowsCc2Flag() {
	ResetCase();
	TIMAdapterF4 adapter(&regs, 84000000U);
	WireEvents(adapter);
	ConfigureCapture(adapter, Capture(
		TIMAdapterF4::Channel::C2,
		LL_TIM_ICPSC_DIV1,
		LL_TIM_IC_FILTER_FDIV1,
		TIMAdapterF4::InputPolarity::Rising,
		TIMAdapterF4::InputSelection::Direct
	));
	EXPECT(adapter.EnableInterrupt({ TIMAdapterF4::Interrupt::CaptureCompare2 }) == ResultStatus::ok);

	// Only the channel 1 flag is raised. Its interrupt is off, and the channel 2 handler must not read it.
	regs.SR |= TIM_SR_CC1IF;
	adapter.IrqHandler();
	EXPECT_EQ(inputEvents.size(), 0);
	EXPECT_EQ(outputEvents.size(), 0);

	regs.SR |= TIM_SR_CC2IF;
	adapter.IrqHandler();
	EXPECT_EQ(inputEvents.size(), 1);
	if (inputEvents.size() == 1) {
		EXPECT_EQ(inputEvents[0], 2);
	}
	EXPECT((regs.SR & TIM_SR_CC2IF) == 0);
	EXPECT_EQ(outputEvents.size(), 0);
}



// Channel 2 as an output: its flag is an output compare event, not an input one.
void TestCompare2EventWhenChannel2IsOutput() {
	ResetCase();
	TIMAdapterF4 adapter(&regs, 84000000U);
	WireEvents(adapter);
	EXPECT(adapter.EnableInterrupt({ TIMAdapterF4::Interrupt::CaptureCompare2 }) == ResultStatus::ok);

	regs.SR |= TIM_SR_CC2IF;
	adapter.IrqHandler();

	EXPECT_EQ(inputEvents.size(), 0);
	EXPECT_EQ(outputEvents.size(), 1);
	if (outputEvents.size() == 1) {
		EXPECT_EQ(outputEvents[0], 2);
	}
}



// Channel 4 as an input reads its select from CCMR2, so its flag is an input event.
void TestCapture4EventFollowsCcmr2() {
	ResetCase();
	TIMAdapterF4 adapter(&regs, 84000000U);
	WireEvents(adapter);
	ConfigureCapture(adapter, Capture(
		TIMAdapterF4::Channel::C4,
		LL_TIM_ICPSC_DIV1,
		LL_TIM_IC_FILTER_FDIV1,
		TIMAdapterF4::InputPolarity::Rising,
		TIMAdapterF4::InputSelection::Direct
	));
	EXPECT(adapter.EnableInterrupt({ TIMAdapterF4::Interrupt::CaptureCompare4 }) == ResultStatus::ok);

	regs.SR |= TIM_SR_CC4IF;
	adapter.IrqHandler();

	EXPECT_EQ(inputEvents.size(), 1);
	if (inputEvents.size() == 1) {
		EXPECT_EQ(inputEvents[0], 4);
	}
	EXPECT_EQ(outputEvents.size(), 0);
	EXPECT((regs.SR & TIM_SR_CC4IF) == 0);
}



// Channel 4 as an output: the CCMR2 select is 0, so the event is an output compare event.
void TestCompare4EventWhenChannel4IsOutput() {
	ResetCase();
	TIMAdapterF4 adapter(&regs, 84000000U);
	WireEvents(adapter);
	EXPECT(adapter.EnableInterrupt({ TIMAdapterF4::Interrupt::CaptureCompare4 }) == ResultStatus::ok);

	regs.SR |= TIM_SR_CC4IF;
	adapter.IrqHandler();

	EXPECT_EQ(inputEvents.size(), 0);
	EXPECT_EQ(outputEvents.size(), 1);
	if (outputEvents.size() == 1) {
		EXPECT_EQ(outputEvents[0], 4);
	}
}



// The clock division is written to CR1 of the timer, and a later call replaces it.
void TestDivisionIsWrittenToTimer() {
	ResetCase();
	TIMAdapterF4 adapter(&regs, 84000000U);

	adapter.SetDivision(TIMAdapterF4::ClockDivision::D4);
	EXPECT_EQ(regs.CR1 & TIM_CR1_CKD, LL_TIM_CLOCKDIVISION_DIV4);

	adapter.SetDivision(TIMAdapterF4::ClockDivision::D2);
	EXPECT_EQ(regs.CR1 & TIM_CR1_CKD, LL_TIM_CLOCKDIVISION_DIV2);

	adapter.SetDivision(TIMAdapterF4::ClockDivision::D1);
	EXPECT_EQ(regs.CR1 & TIM_CR1_CKD, LL_TIM_CLOCKDIVISION_DIV1);
}



// The capture value of each channel is the capture register of that channel.
void TestGetCaptureReadsCaptureRegister() {
	ResetCase();
	TIMAdapterF4 adapter(&regs, 84000000U);
	regs.CCR1 = 1234;
	regs.CCR2 = 2345;
	regs.CCR3 = 3456;
	regs.CCR4 = 99;

	EXPECT_EQ(adapter.GetCapture(TIMAdapterF4::Channel::C1), 1234);
	EXPECT_EQ(adapter.GetCapture(TIMAdapterF4::Channel::C2), 2345);
	EXPECT_EQ(adapter.GetCapture(TIMAdapterF4::Channel::C3), 3456);
	EXPECT_EQ(adapter.GetCapture(TIMAdapterF4::Channel::C4), 99);
}


} // namespace


int main() {
	TestChannel1DirectRisingWritesCcmr1();
	TestChannel1PrescalerFilterAndSelection();
	TestPolarityBitsPerChannel();
	TestChannel2UsesHighByteOfCcmr1();
	TestChannels3And4UseCcmr2();
	TestZeroRemappingLeavesOrAlone();
	TestRemappingIsWrittenToOr();
	TestCapture2EventFollowsCc2Flag();
	TestCompare2EventWhenChannel2IsOutput();
	TestCapture4EventFollowsCcmr2();
	TestCompare4EventWhenChannel4IsOutput();
	TestDivisionIsWrittenToTimer();
	TestGetCaptureReadsCaptureRegister();

	if (failureCount != 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("all TIM checks passed\n");
	return 0;
}
