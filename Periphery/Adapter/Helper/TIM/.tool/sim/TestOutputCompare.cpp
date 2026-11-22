#include <TIMOutputCompareHelper.h>
#include <Utilities/Math/IQMath/IQ.h>
#include <cstdio>


// Host tests for TIMOutputCompareHelper: the frequency and duty it writes, and what GetFrequencyInfo reads back. The clock is
// 32 MHz in Hz. The expected values are worked out by hand in each case.


namespace {


int failureCount = 0;


void ExpectValue(double actual, double expected, const char* name, int line) {
	// The values are exact in these cases, so a tolerance only guards the float rounding of 1000.0f and 25.0f.
	double difference = actual - expected;
	if (difference < 0.0) {
		difference = -difference;
	}

	if (difference > 1e-3) {
		failureCount++;
		std::fprintf(stderr, "TestOutputCompare.cpp:%d: %s is %.6f, expected %.6f\n", line, name, actual, expected);
	}
}


void ExpectEqual(unsigned long long actual, unsigned long long expected, const char* name, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestOutputCompare.cpp:%d: %s is %llu, expected %llu\n", line, name, actual, expected);
	}
}


template <RealType Type>
void SixteenBitCases() {
	ATIM adapter(32000000u, ATIM::Bitness::B16, ATIM::ClockDivisionOption::Div1);
	TIMOutputCompareHelper<Type> pwm(adapter, ATIM::ChannelOption::Ch1);

	// 1 kHz at 25 %. From the base frequency: prescaler 0, period 31999. The pulse is (31999 + 1) * 25 / 100 = 8000 ticks.
	pwm.SetFrequencyInfo({ .frequencyHz = Type(1000.0), .duty = Type(25.0) });
	ExpectEqual(adapter.GetParameters().prescaler, 0, "prescaler after 1 kHz", __LINE__);
	ExpectEqual(adapter.GetParameters().period, 31999, "period after 1 kHz", __LINE__);
	ExpectEqual(adapter.GetOutputCompareParameters(ATIM::ChannelOption::Ch1).compare, 8000, "compare after 25 %", __LINE__);

	// Read back: 32000000 / 1 / 1 / 32000 = 1000 Hz. The duty is 8000 / 32000 * 100 = 25 %.
	Type readFrequency = pwm.GetFrequencyInfo().frequencyHz;
	Type readDuty = pwm.GetFrequencyInfo().duty;
	ExpectValue(static_cast<double>(readFrequency), 1000.0, "frequency after 1 kHz", __LINE__);
	ExpectValue(static_cast<double>(readDuty), 25.0, "duty after 1 kHz", __LINE__);

	// SetDuty(50): the pulse is 32000 * 50 / 100 = 16000 ticks, and the frequency stays at 1000 Hz.
	pwm.SetDuty(Type(50.0));
	ExpectEqual(adapter.GetOutputCompareParameters(ATIM::ChannelOption::Ch1).compare, 16000, "compare after 50 %", __LINE__);
	ExpectValue(static_cast<double>(pwm.GetFrequencyInfo().duty), 50.0, "duty after SetDuty", __LINE__);

	// SetFrequency(2000) keeps the 50 %. 32000000 / 2000 = 16000 ticks, so the period is 15999 at prescaler 0.
	// The pulse is 16000 * 50 / 100 = 8000 ticks.
	pwm.SetFrequency(Type(2000.0));
	ExpectEqual(adapter.GetParameters().period, 15999, "period after 2 kHz", __LINE__);
	ExpectEqual(adapter.GetOutputCompareParameters(ATIM::ChannelOption::Ch1).compare, 8000, "compare after 2 kHz", __LINE__);
	ExpectValue(static_cast<double>(pwm.GetFrequencyInfo().frequencyHz), 2000.0, "frequency after SetFrequency", __LINE__);
	ExpectValue(static_cast<double>(pwm.GetFrequencyInfo().duty), 50.0, "duty after SetFrequency", __LINE__);

	// TunePrescaler with period 31999: the clock divided by the period gives 32000000 / 32000 = 1000 ticks per second at
	// prescaler 0. For 1 kHz the register is 0, and the readback is 32000000 / 1 / 32000 = 1000 Hz.
	ATIM tuneAdapter(32000000u, ATIM::Bitness::B16, ATIM::ClockDivisionOption::Div1);
	TIMOutputCompareHelper<Type> tune(tuneAdapter, ATIM::ChannelOption::Ch1);
	tune.SetFrequencyInfo({ .frequencyHz = Type(1000.0), .duty = Type(50.0) });
	tune.TunePrescaler(Type(1000.0));
	ExpectEqual(tuneAdapter.GetParameters().prescaler, 0, "prescaler after TunePrescaler 1 kHz", __LINE__);
	ExpectValue(static_cast<double>(tune.GetFrequencyInfo().frequencyHz), 1000.0, "frequency after TunePrescaler", __LINE__);

	// TunePrescaler with period 9: 32000000 / 10 = 3200000 ticks per second at prescaler 0. A 1 Hz target needs a prescaler
	// of 3199999, above the 16-bit range, so the register is the largest value 0xFFFF.
	ATIM clampAdapter(32000000u, ATIM::Bitness::B16, ATIM::ClockDivisionOption::Div1);
	clampAdapter.SetPeriod(9);
	TIMOutputCompareHelper<Type> clamp(clampAdapter, ATIM::ChannelOption::Ch1);
	clamp.TunePrescaler(Type(1.0));
	ExpectEqual(clampAdapter.GetParameters().prescaler, 0xFFFF, "prescaler clamped by TunePrescaler at 1 Hz", __LINE__);

	// A negative frequency is not a valid target. The register is clamped to 0, not converted from a negative value.
	ATIM negativeAdapter(32000000u, ATIM::Bitness::B16, ATIM::ClockDivisionOption::Div1);
	TIMOutputCompareHelper<Type> negative(negativeAdapter, ATIM::ChannelOption::Ch1);
	negative.SetFrequencyInfo({ .frequencyHz = Type(1000.0), .duty = Type(50.0) });
	negative.TunePrescaler(Type(-5.0));
	ExpectEqual(negativeAdapter.GetParameters().prescaler, 0, "prescaler after TunePrescaler of a negative frequency", __LINE__);

	// 100 % duty at the slowest output: the pulse is 65536 ticks, which does not fit in 16 bits. The compare is the
	// largest 16-bit value, not a wrapped 0.
	ATIM fullAdapter(32000000u, ATIM::Bitness::B16, ATIM::ClockDivisionOption::Div1);
	TIMOutputCompareHelper<Type> full(fullAdapter, ATIM::ChannelOption::Ch1);
	full.SetFrequencyInfo({ .frequencyHz = Type(1.0 / 256.0), .duty = Type(100.0) });
	ExpectEqual(fullAdapter.GetParameters().prescaler, 0xFFFF, "prescaler at the slowest output", __LINE__);
	ExpectEqual(fullAdapter.GetParameters().period, 0xFFFF, "period at the slowest output", __LINE__);
	ExpectEqual(fullAdapter.GetOutputCompareParameters(ATIM::ChannelOption::Ch1).compare, 0xFFFF, "compare at 100 % of the slowest output", __LINE__);
}


void ThirtyTwoBitCases() {
	// The target 1e-7 Hz gives 32000000 / 1e-7 = 3.2e14 ticks, so the period is clamped to 0xFFFFFFFF. A 50 % duty then
	// needs (4294967295 + 1) * 50 / 100 = 2147483648 ticks. The period plus one must not wrap to 0 in uint32.
	ATIM adapter(32000000u, ATIM::Bitness::B32, ATIM::ClockDivisionOption::Div1);
	TIMOutputCompareHelper<float> pwm(adapter, ATIM::ChannelOption::Ch1);

	pwm.SetFrequencyInfo({ .frequencyHz = 0.0000001f, .duty = 50.0f });
	ExpectEqual(adapter.GetParameters().period, 4294967295u, "32-bit period at the lowest frequency", __LINE__);
	ExpectEqual(adapter.GetOutputCompareParameters(ATIM::ChannelOption::Ch1).compare, 2147483648u, "32-bit compare at 50 %", __LINE__);

	// 100 % duty at the slowest output: the pulse is 4294967296 ticks, which does not fit in 32 bits. The compare is the
	// largest 32-bit value.
	ATIM fullAdapter(32000000u, ATIM::Bitness::B32, ATIM::ClockDivisionOption::Div1);
	TIMOutputCompareHelper<float> full(fullAdapter, ATIM::ChannelOption::Ch1);
	full.SetFrequencyInfo({ .frequencyHz = 1.0f / 256.0f, .duty = 100.0f });
	ExpectEqual(fullAdapter.GetParameters().period, 4294967295u, "32-bit period at 1/256 Hz", __LINE__);
	ExpectEqual(fullAdapter.GetOutputCompareParameters(ATIM::ChannelOption::Ch1).compare, 4294967295u, "32-bit compare at 100 %", __LINE__);
}


}


int main() {
	SixteenBitCases<float>();
	SixteenBitCases<IQ<16>>();
	ThirtyTwoBitCases();

	if (failureCount != 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("all checks passed\n");
	return 0;
}
