#include <TIMOutputCompareHelper.h>
#include <Utilities/Math/IQMath/IQ.h>
#include <cstdio>


// A float or IQ value as a double. An IQ is read through its raw value, so the read adds no rounding.
template <typename Number>
double AsDouble(Number value) {
	if constexpr (IQType<Number>) {
		return static_cast<double>(value.raw) / static_cast<double>(Number::one);
	} else {
		return static_cast<double>(value);
	}
}


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
	ExpectValue(AsDouble(readFrequency), 1000.0, "frequency after 1 kHz", __LINE__);
	ExpectValue(AsDouble(readDuty), 25.0, "duty after 1 kHz", __LINE__);

	// SetDuty(50): the pulse is 32000 * 50 / 100 = 16000 ticks, and the frequency stays at 1000 Hz.
	pwm.SetDuty(Type(50.0));
	ExpectEqual(adapter.GetOutputCompareParameters(ATIM::ChannelOption::Ch1).compare, 16000, "compare after 50 %", __LINE__);
	ExpectValue(AsDouble(pwm.GetFrequencyInfo().duty), 50.0, "duty after SetDuty", __LINE__);

	// SetFrequency(2000) keeps the 50 %. 32000000 / 2000 = 16000 ticks, so the period is 15999 at prescaler 0.
	// The pulse is 16000 * 50 / 100 = 8000 ticks.
	pwm.SetFrequency(Type(2000.0));
	ExpectEqual(adapter.GetParameters().period, 15999, "period after 2 kHz", __LINE__);
	ExpectEqual(adapter.GetOutputCompareParameters(ATIM::ChannelOption::Ch1).compare, 8000, "compare after 2 kHz", __LINE__);
	ExpectValue(AsDouble(pwm.GetFrequencyInfo().frequencyHz), 2000.0, "frequency after SetFrequency", __LINE__);
	ExpectValue(AsDouble(pwm.GetFrequencyInfo().duty), 50.0, "duty after SetFrequency", __LINE__);

	// TunePrescaler with period 31999: the clock divided by the period gives 32000000 / 32000 = 1000 ticks per second at
	// prescaler 0. For 1 kHz the register is 0, and the readback is 32000000 / 1 / 32000 = 1000 Hz.
	ATIM tuneAdapter(32000000u, ATIM::Bitness::B16, ATIM::ClockDivisionOption::Div1);
	TIMOutputCompareHelper<Type> tune(tuneAdapter, ATIM::ChannelOption::Ch1);
	tune.SetFrequencyInfo({ .frequencyHz = Type(1000.0), .duty = Type(50.0) });
	tune.TunePrescaler(Type(1000.0));
	ExpectEqual(tuneAdapter.GetParameters().prescaler, 0, "prescaler after TunePrescaler 1 kHz", __LINE__);
	ExpectValue(AsDouble(tune.GetFrequencyInfo().frequencyHz), 1000.0, "frequency after TunePrescaler", __LINE__);

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


// IQ<24> has a range of about +-128, so its targets here are 127 Hz or less. Its raw unit is 2^-24 Hz.
void Q24Cases() {
	using Type = IQ<24>;

	// 2.5 Hz at 16 bits: prescaler 195 and period 65305, as in the frequency tests. The pulse at 50 % is 65306 * 50 / 100 = 32653.
	ATIM adapter(32000000u, ATIM::Bitness::B16, ATIM::ClockDivisionOption::Div1);
	TIMOutputCompareHelper<Type> pwm(adapter, ATIM::ChannelOption::Ch1);
	pwm.SetFrequencyInfo({ .frequencyHz = Type(2.5), .duty = Type(50.0) });
	ExpectEqual(adapter.GetParameters().prescaler, 195, "IQ<24> prescaler at 2.5 Hz", __LINE__);
	ExpectEqual(adapter.GetParameters().period, 65305, "IQ<24> period at 2.5 Hz", __LINE__);
	ExpectEqual(adapter.GetOutputCompareParameters(ATIM::ChannelOption::Ch1).compare, 32653, "IQ<24> compare at 50 %", __LINE__);

	// Read back: 32000000 * 2^24 / (196 * 65306) = 41943117 raw units, which is 2.5000046 Hz. The duty is exactly 50 %.
	ExpectValue(AsDouble(pwm.GetFrequencyInfo().frequencyHz), 2.5, "IQ<24> frequency after 2.5 Hz", __LINE__);
	ExpectValue(AsDouble(pwm.GetFrequencyInfo().duty), 50.0, "IQ<24> duty after 2.5 Hz", __LINE__);

	// Raw 2 is 2^-23 Hz. The counts are 32000000 * 2^23, about 2.7e14, far above 2^32, so the period is 0xFFFFFFFF. A duty of
	// 100 % is 2^32 ticks, clamped to 0xFFFFFFFF. The product (2^32) * (100 << 24), about 7.2e18, is below 2^63.
	ATIM wide(32000000u, ATIM::Bitness::B32, ATIM::ClockDivisionOption::Div1);
	TIMOutputCompareHelper<Type> wideFull(wide, ATIM::ChannelOption::Ch1);
	wideFull.SetFrequencyInfo({ .frequencyHz = Type::FromRaw(2), .duty = Type(100.0) });
	ExpectEqual(wide.GetParameters().period, 0xFFFFFFFFu, "IQ<24> 32-bit period at raw 2", __LINE__);
	ExpectEqual(wide.GetOutputCompareParameters(ATIM::ChannelOption::Ch1).compare, 0xFFFFFFFFu, "IQ<24> 32-bit compare at 100 %", __LINE__);

	// The same target at 50 % is 2^32 * 50 / 100 = 2147483648 ticks.
	ATIM wideHalf(32000000u, ATIM::Bitness::B32, ATIM::ClockDivisionOption::Div1);
	TIMOutputCompareHelper<Type> wideHalfHelper(wideHalf, ATIM::ChannelOption::Ch1);
	wideHalfHelper.SetFrequencyInfo({ .frequencyHz = Type::FromRaw(2), .duty = Type(50.0) });
	ExpectEqual(wideHalf.GetOutputCompareParameters(ATIM::ChannelOption::Ch1).compare, 2147483648u, "IQ<24> 32-bit compare at 50 %", __LINE__);
}


// The duty read back from a compare that does not divide the period exactly. The raw duty is floor(compare * 100 * 2^q / period),
// so the readback must match that floor to the last raw unit, not only to the 1e-3 tolerance of ExpectValue.
void NonExactDutyCases() {
	// Period 2, so 3 ticks per cycle, and compare 1. The duty is 100 / 3 = 33.3333... %. In IQ<16> the raw value is
	// floor(100 * 65536 / 3) = 2184533, not the 2184500 that truncating the fraction before the multiplication gives.
	ATIM adapter(32000000u, ATIM::Bitness::B16, ATIM::ClockDivisionOption::Div1);
	adapter.SetPeriod(2);
	adapter.SetCompare(ATIM::ChannelOption::Ch1, 1);
	TIMOutputCompareHelper<IQ<16>> pwm(adapter, ATIM::ChannelOption::Ch1);

	ExpectEqual(static_cast<unsigned long long>(pwm.GetFrequencyInfo().duty.raw), 2184533ull, "IQ<16> duty raw at 1 of 3 ticks", __LINE__);
}


// TuneHalfCompare builds its target as twice the frequency. For an IQ type the target is shifted into raw units in 64 bits,
// so a target above the range of the type is not saturated before the search.
template <RealType Type>
void TuneHalfCompareCases() {
	// Period 79, so 80 ticks per cycle. The target is 2 * 20000 = 40000 Hz, the counts are 32000000 / (80 * 40000) = 10, and the
	// register is 9. The compare is 80 / 2 = 40. An iq16 target of 40000 Hz would saturate at 32767.99998 Hz, which gives counts 12
	// and register 11.
	ATIM high(32000000u, ATIM::Bitness::B16, ATIM::ClockDivisionOption::Div1);
	high.SetPeriod(79);
	TIMOutputCompareHelper<Type> highHelper(high, ATIM::ChannelOption::Ch1);
	highHelper.TuneHalfCompare(20000);
	ExpectEqual(high.GetParameters().prescaler, 9, "prescaler after TuneHalfCompare 20 kHz", __LINE__);
	ExpectEqual(high.GetOutputCompareParameters(ATIM::ChannelOption::Ch1).compare, 40, "compare after TuneHalfCompare 20 kHz", __LINE__);

	// Period 3199, so 3200 ticks per cycle. The target is 2 * 50 = 100 Hz, the counts are 32000000 / (3200 * 100) = 100, and the
	// register is 99. The compare is 3200 / 2 = 1600.
	ATIM low(32000000u, ATIM::Bitness::B16, ATIM::ClockDivisionOption::Div1);
	low.SetPeriod(3199);
	TIMOutputCompareHelper<Type> lowHelper(low, ATIM::ChannelOption::Ch1);
	lowHelper.TuneHalfCompare(50);
	ExpectEqual(low.GetParameters().prescaler, 99, "prescaler after TuneHalfCompare 50 Hz", __LINE__);
	ExpectEqual(low.GetOutputCompareParameters(ATIM::ChannelOption::Ch1).compare, 1600, "compare after TuneHalfCompare 50 Hz", __LINE__);

	// A zero target has an infinite count in the float path, so the register is the largest value.
	ATIM zero(32000000u, ATIM::Bitness::B16, ATIM::ClockDivisionOption::Div1);
	zero.SetPeriod(79);
	TIMOutputCompareHelper<Type> zeroHelper(zero, ATIM::ChannelOption::Ch1);
	zeroHelper.TuneHalfCompare(0);
	ExpectEqual(zero.GetParameters().prescaler, 0xFFFF, "prescaler after TuneHalfCompare 0 Hz", __LINE__);
}


// The edges of the 32-bit range: the half compare of period 0xFFFFFFFF, and a very large TuneHalfCompare target.
void WrapCases() {
	// The period 0xFFFFFFFF plus one is 2^32. In uint32 that sum would wrap to 0 and give a compare of 1. The compare is 2^31.
	ATIM top(32000000u, ATIM::Bitness::B32, ATIM::ClockDivisionOption::Div1);
	top.SetPeriod(0xFFFFFFFFu);
	TIMOutputCompareHelper<float> topHelper(top, ATIM::ChannelOption::Ch1);
	topHelper.SetHalfCompare();
	ExpectEqual(top.GetOutputCompareParameters(ATIM::ChannelOption::Ch1).compare, 2147483648u, "half compare at period 0xFFFFFFFF", __LINE__);

	// The 16-bit period 0xFFFF gives 65536 / 2 = 32768.
	ATIM narrow(32000000u, ATIM::Bitness::B16, ATIM::ClockDivisionOption::Div1);
	narrow.SetPeriod(0xFFFFu);
	TIMOutputCompareHelper<float> narrowHelper(narrow, ATIM::ChannelOption::Ch1);
	narrowHelper.SetHalfCompare();
	ExpectEqual(narrow.GetOutputCompareParameters(ATIM::ChannelOption::Ch1).compare, 32768, "half compare at period 0xFFFF", __LINE__);

	// The target 2 * 4294967295 Hz is about 2^33, and in IQ<30> its raw value is about 2^63, which must not wrap. The counts are
	// 32000000 / (80 * 8589934590), far below 1, so the register is 0 and the compare is 40.
	ATIM high(32000000u, ATIM::Bitness::B16, ATIM::ClockDivisionOption::Div1);
	high.SetPeriod(79);
	TIMOutputCompareHelper<IQ<30>> highHelper(high, ATIM::ChannelOption::Ch1);
	highHelper.TuneHalfCompare(4294967295u);
	ExpectEqual(high.GetParameters().prescaler, 0, "prescaler at the largest TuneHalfCompare target", __LINE__);
	ExpectEqual(high.GetOutputCompareParameters(ATIM::ChannelOption::Ch1).compare, 40, "compare at the largest TuneHalfCompare target", __LINE__);
}


// Extremes for TunePrescaler, where the period plus one is up to 2^32 and the raw target up to 2^30 in IQ<30>.
void TunePrescalerCases() {
	// 32 bits, division 4, clock 2^32 - 1 Hz, period 0xFFFFFFFF, target 1 Hz. The ticks per second at prescaler 0 are
	// (2^32 - 1) / (4 * 2^32) = 0.25, so the register is 0. The product 4 * 2^32 * 2^30 would be 2^64 and wrap to 0.
	ATIM wide(4294967295u, ATIM::Bitness::B32, ATIM::ClockDivisionOption::Div4);
	wide.SetPeriod(0xFFFFFFFFu);
	TIMOutputCompareHelper<IQ<30>> wideHelper(wide, ATIM::ChannelOption::Ch1);
	wideHelper.TunePrescaler(IQ<30>::FromRaw(1 << 30));
	ExpectEqual(wide.GetParameters().prescaler, 0, "prescaler at period 0xFFFFFFFF, division 4, 1 Hz", __LINE__);

	// 16 bits, division 1, clock 2^32 - 1 Hz, period 0xFFFF, target 1 Hz. The ticks per second at prescaler 0 are
	// (2^32 - 1) / 65536 = 65535.99998, so the register is 65534.
	ATIM narrow(4294967295u, ATIM::Bitness::B16, ATIM::ClockDivisionOption::Div1);
	narrow.SetPeriod(0xFFFF);
	TIMOutputCompareHelper<IQ<30>> narrowHelper(narrow, ATIM::ChannelOption::Ch1);
	narrowHelper.TunePrescaler(IQ<30>::FromRaw(1 << 30));
	ExpectEqual(narrow.GetParameters().prescaler, 65534, "prescaler at period 0xFFFF, 1 Hz", __LINE__);
}


// Compares the prescaler, period and channel compare of two adapters.
void ExpectSameRegisters(ATIM& expected, ATIM& actual, const char* name, int line) {
	ExpectEqual(actual.GetParameters().prescaler, expected.GetParameters().prescaler, name, line);
	ExpectEqual(actual.GetParameters().period, expected.GetParameters().period, name, line);
	ExpectEqual(
		actual.GetOutputCompareParameters(ATIM::ChannelOption::Ch1).compare,
		expected.GetOutputCompareParameters(ATIM::ChannelOption::Ch1).compare,
		name,
		line
	);
}


// Targets that are exact in float, iq16 and iq24 must write the same registers. The two IQ types are integer and must always
// match each other. Float is compared only when the counts at prescaler 0 are below 2^24, where a float period of 64000000 - 1
// would already be stored as 64000000 above it. The bound is not a proof of float exactness: float can round below 2^24 too,
// so a float mismatch in this sweep is a float rounding, and the IQ types are the reference.
void SameRegisterCases() {
	const uint32 clocks[] = { 32000000u, 65536u, 32u };
	const ATIM::Bitness bitnesses[] = { ATIM::Bitness::B16, ATIM::Bitness::B32 };
	const ATIM::ClockDivisionOption divisions[] = { ATIM::ClockDivisionOption::Div1, ATIM::ClockDivisionOption::Div2, ATIM::ClockDivisionOption::Div4 };
	const double targets[] = { 2.5, 0.5, 64.0, 125.0, 1.0 / 256.0 };
	const double duties[] = { 25.0, 12.5 };
	const double exactFloatCounts = 16777216.0;

	for (uint32 clockHz : clocks) {
		for (ATIM::Bitness bitness : bitnesses) {
			for (ATIM::ClockDivisionOption division : divisions) {
				double divisionValue = static_cast<double>(static_cast<int>(division));

				for (double targetHz : targets) {
					bool floatIsExact = static_cast<double>(clockHz) / (divisionValue * targetHz) < exactFloatCounts;

					for (double dutyPercent : duties) {
						ATIM floatAdapter(clockHz, bitness, division);
						ATIM iq16Adapter(clockHz, bitness, division);
						ATIM iq24Adapter(clockHz, bitness, division);

						TIMOutputCompareHelper<float> floatHelper(floatAdapter, ATIM::ChannelOption::Ch1);
						TIMOutputCompareHelper<IQ<16>> iq16Helper(iq16Adapter, ATIM::ChannelOption::Ch1);
						TIMOutputCompareHelper<IQ<24>> iq24Helper(iq24Adapter, ATIM::ChannelOption::Ch1);

						floatHelper.SetFrequencyInfo({ .frequencyHz = static_cast<float>(targetHz), .duty = static_cast<float>(dutyPercent) });
						iq16Helper.SetFrequencyInfo({ .frequencyHz = RealFrom<IQ<16>>(targetHz), .duty = RealFrom<IQ<16>>(dutyPercent) });
						iq24Helper.SetFrequencyInfo({ .frequencyHz = RealFrom<IQ<24>>(targetHz), .duty = RealFrom<IQ<24>>(dutyPercent) });

						ExpectSameRegisters(iq16Adapter, iq24Adapter, "iq16 and iq24 registers", __LINE__);

						if (floatIsExact) {
							ExpectSameRegisters(floatAdapter, iq16Adapter, "float and iq16 registers", __LINE__);
							ExpectSameRegisters(floatAdapter, iq24Adapter, "float and iq24 registers", __LINE__);
						}
					}
				}
			}
		}
	}
}


}


int main() {
	SixteenBitCases<float>();
	SixteenBitCases<IQ<16>>();
	ThirtyTwoBitCases();
	Q24Cases();
	NonExactDutyCases();
	TuneHalfCompareCases<float>();
	TuneHalfCompareCases<IQ<16>>();
	TuneHalfCompareCases<IQ<24>>();
	WrapCases();
	TunePrescalerCases();
	SameRegisterCases();

	if (failureCount != 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("all checks passed\n");
	return 0;
}
