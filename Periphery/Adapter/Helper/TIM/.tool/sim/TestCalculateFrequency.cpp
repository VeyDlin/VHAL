#include <ITIMHelper.h>
#include <Utilities/Math/IQMath/IQ.h>
#include <cstdio>
#include <utility>


// Host tests for ITIMHelper::CalculateFrequency and SetBaseFrequency. The clock is in Hz, as TIMAdapter reports it.
// Each expected pair is worked out by hand from period = clock / (division * (prescaler + 1) * target) - 1: the 16-bit
// timer takes the smallest prescaler whose period fits in 16 bits, and the 32-bit timer always takes prescaler 0.


namespace {


int failureCount = 0;


template <RealType Type>
struct FrequencyProbe : public ITIMHelper<Type> {
	using ITIMHelper<Type>::CalculateFrequency;
};


// Runs CalculateFrequency for the target in Hz, converted to Type first, and checks the prescaler and the period.
// The target must be exact in Type, so the case tests the stated frequency and not a rounded one.
template <RealType Type>
void ExpectPair(ATIM& adapter, double targetHz, uint16 expectedPrescaler, uint32 expectedPeriod, int line) {
	Type target(targetHz);
	if (static_cast<double>(target) != targetHz) {
		failureCount++;
		std::fprintf(stderr, "TestCalculateFrequency.cpp:%d: target %.9f is not exact in this type\n", line, targetHz);
	}

	std::pair<uint16, uint32> actual = FrequencyProbe<Type>::CalculateFrequency(&adapter, target);

	if (actual.first != expectedPrescaler || actual.second != expectedPeriod) {
		failureCount++;
		std::fprintf(stderr, "TestCalculateFrequency.cpp:%d: prescaler %u period %u, expected prescaler %u period %u\n", line,
			static_cast<unsigned>(actual.first), static_cast<unsigned>(actual.second), static_cast<unsigned>(expectedPrescaler),
			static_cast<unsigned>(expectedPeriod));
	}
}


// Runs SetBaseFrequency and checks what the adapter received.
template <RealType Type>
void ExpectBaseFrequency(ATIM& adapter, double targetHz, uint16 expectedPrescaler, uint32 expectedPeriod, int line) {
	ITIMHelper<Type> helper(adapter, ATIM::ChannelOption::Ch1);
	helper.SetBaseFrequency(Type(targetHz));

	ATIM::Parameters parameters = adapter.GetParameters();
	if (parameters.prescaler != expectedPrescaler || parameters.period != expectedPeriod) {
		failureCount++;
		std::fprintf(stderr, "TestCalculateFrequency.cpp:%d: adapter got prescaler %u period %u, expected prescaler %u period %u\n",
			line, static_cast<unsigned>(parameters.prescaler), static_cast<unsigned>(parameters.period),
			static_cast<unsigned>(expectedPrescaler), static_cast<unsigned>(expectedPeriod));
	}
}


template <RealType Type>
void SixteenBitCases() {
	// 32 MHz, division 1, 1 kHz. Prescaler 0 gives 32000000 / 1000 = 32000 ticks, so the period is 31999.
	ATIM fast(32000000u, ATIM::Bitness::B16, ATIM::ClockDivisionOption::Div1);
	ExpectPair<Type>(fast, 1000.0, 0, 31999, __LINE__);

	// 32 MHz, division 1, 2.5 Hz. Prescaler 194 gives 32000000 / (195 * 2.5) = 65641.03 ticks, a period of 65640: too big.
	// Prescaler 195 gives 32000000 / (196 * 2.5) = 65306.12 ticks, a period of 65305, which fits.
	ATIM slow(32000000u, ATIM::Bitness::B16, ATIM::ClockDivisionOption::Div1);
	ExpectPair<Type>(slow, 2.5, 195, 65305, __LINE__);

	// Division 2, 1 kHz: 32000000 / (2 * 1000) = 16000 ticks, period 15999 at prescaler 0.
	ATIM divide2(32000000u, ATIM::Bitness::B16, ATIM::ClockDivisionOption::Div2);
	ExpectPair<Type>(divide2, 1000.0, 0, 15999, __LINE__);

	// Division 4, 1 kHz: 32000000 / (4 * 1000) = 8000 ticks, period 7999 at prescaler 0.
	ATIM divide4(32000000u, ATIM::Bitness::B16, ATIM::ClockDivisionOption::Div4);
	ExpectPair<Type>(divide4, 1000.0, 0, 7999, __LINE__);

	// Division 2, 2.5 Hz: 32000000 / (2 * (psc + 1) * 2.5) = 6400000 / (psc + 1). Prescaler 96 gives 6400000 / 97 = 65979.4,
	// a period of 65978: too big. Prescaler 97 gives 6400000 / 98 = 65306.12, a period of 65305, which fits.
	ATIM divide2Slow(32000000u, ATIM::Bitness::B16, ATIM::ClockDivisionOption::Div2);
	ExpectPair<Type>(divide2Slow, 2.5, 97, 65305, __LINE__);

	// Boundary: 65536 ticks per period at prescaler 0 gives period 65535, the largest 16-bit value.
	ATIM boundary(65536u, ATIM::Bitness::B16, ATIM::ClockDivisionOption::Div1);
	ExpectPair<Type>(boundary, 1.0, 0, 65535, __LINE__);

	// No prescaler fits: 32000000 / (65536 * (1 / 256)) = 125000 ticks even at the largest prescaler. The target 1 / 256 Hz
	// is exact in every type. The result is the slowest output, prescaler 0xFFFF and period 0xFFFF.
	ATIM tooSlow(32000000u, ATIM::Bitness::B16, ATIM::ClockDivisionOption::Div1);
	ExpectPair<Type>(tooSlow, 1.0 / 256.0, 0xFFFF, 0xFFFF, __LINE__);

	// Period 0 is never used. 32000 / 32000 = 1 tick per period would give period 0, so it becomes 1.
	ATIM oneTick(32000u, ATIM::Bitness::B16, ATIM::ClockDivisionOption::Div1);
	ExpectPair<Type>(oneTick, 32000.0, 0, 1, __LINE__);

	// Above the clock: 32 / 64 = 0.5 ticks per period. The period is clamped to 1, the fastest value.
	ATIM tooFast(32u, ATIM::Bitness::B16, ATIM::ClockDivisionOption::Div1);
	ExpectPair<Type>(tooFast, 64.0, 0, 1, __LINE__);

	ExpectBaseFrequency<Type>(fast, 1000.0, 0, 31999, __LINE__);
	ExpectBaseFrequency<Type>(slow, 2.5, 195, 65305, __LINE__);
}


template <RealType Type>
void ThirtyTwoBitCases() {
	ATIM fast(32000000u, ATIM::Bitness::B32, ATIM::ClockDivisionOption::Div1);
	// 32000000 / 1000 = 32000 ticks at prescaler 0, period 31999.
	ExpectPair<Type>(fast, 1000.0, 0, 31999, __LINE__);

	// 32000000 / 2.5 = 12800000 ticks, period 12799999. The 32-bit period needs no prescaler.
	ATIM slow(32000000u, ATIM::Bitness::B32, ATIM::ClockDivisionOption::Div1);
	ExpectPair<Type>(slow, 2.5, 0, 12799999, __LINE__);

	// Division 4, 2.5 Hz: 32000000 / (4 * 2.5) = 3200000 ticks, period 3199999.
	ATIM divide4(32000000u, ATIM::Bitness::B32, ATIM::ClockDivisionOption::Div4);
	ExpectPair<Type>(divide4, 2.5, 0, 3199999, __LINE__);

	// 32000000 / (1 / 256) = 8192000000 ticks, more than 2^32. The period is clamped to 0xFFFFFFFF.
	ATIM tooSlow(32000000u, ATIM::Bitness::B32, ATIM::ClockDivisionOption::Div1);
	ExpectPair<Type>(tooSlow, 1.0 / 256.0, 0, 0xFFFFFFFFu, __LINE__);

	// Above the clock: 32 / 64 = 0.5 ticks, clamped to period 1.
	ATIM tooFast(32u, ATIM::Bitness::B32, ATIM::ClockDivisionOption::Div1);
	ExpectPair<Type>(tooFast, 64.0, 0, 1, __LINE__);

	ExpectBaseFrequency<Type>(slow, 2.5, 0, 12799999, __LINE__);
}


}


int main() {
	SixteenBitCases<float>();
	SixteenBitCases<IQ<16>>();
	ThirtyTwoBitCases<float>();
	ThirtyTwoBitCases<IQ<16>>();

	if (failureCount != 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("all checks passed\n");
	return 0;
}
