#include <Utilities/Time/Ticks.h>
#include <VhalCheck.h>


// Host tests of the tick helpers. The uint32 cases sit at the edges of the 32-bit wrap: 0, 2^31 - 1, 2^31 and 2^32 - 1 on both sides of
// each comparison. Each table is checked at compile time and again at run time, so both evaluations give the same answers.


struct ReachedCase {
	uint32 now;
	uint32 due;
	bool expected;
};


struct ElapsedCase {
	uint32 now;
	uint32 since;
	uint32 interval;
	bool expected;
};


constexpr uint32 halfRange = 0x80000000u;
constexpr uint32 maxTick = 0xFFFFFFFFu;


constexpr ReachedCase reachedCases[] = {
	{0u, 0u, true},
	{5u, 5u, true},
	{4u, 5u, false},
	{0x7FFFFFFFu, 0u, true},
	{0u, 0x7FFFFFFFu, false},
	{halfRange, halfRange - 1u, true},
	{halfRange - 1u, halfRange, false},
	{halfRange - 1u, 0u, true},
	{0u, halfRange - 1u, false},
	{0u, maxTick, true},
	{maxTick, 0u, false},
	{maxTick - 1u, maxTick, false},
};


// Two values exactly 2^31 apart are outside the rule: the signed difference is the lowest negative value, so the later value reads as
// the earlier one. The case is kept to pin that edge.
constexpr ReachedCase reachedHalfRangeCases[] = {
	{0u, halfRange, false},
	{halfRange, 0u, false},
};


constexpr ElapsedCase elapsedCases[] = {
	{10u, 0u, 10u, true},
	{9u, 0u, 10u, false},
	{5u, 5u, 0u, true},
	{maxTick, 0u, maxTick, true},
	{maxTick - 1u, 0u, maxTick, false},
	{5u, maxTick - 4u, 10u, true},
	{4u, maxTick - 4u, 10u, false},
	{5u, maxTick - 4u, 11u, false},
	{halfRange, halfRange - 1u, 1u, true},
	{halfRange - 1u, halfRange - 2u, 1u, true},
	{halfRange - 1u, halfRange - 2u, 2u, false},
};


constexpr bool AllReachedHold() {
	for (ReachedCase testCase : reachedCases) {
		if (Ticks::Reached(testCase.now, testCase.due) != testCase.expected) {
			return false;
		}
	}

	for (ReachedCase testCase : reachedHalfRangeCases) {
		if (Ticks::Reached(testCase.now, testCase.due) != testCase.expected) {
			return false;
		}
	}

	return true;
}


constexpr bool AllElapsedHold() {
	for (ElapsedCase testCase : elapsedCases) {
		if (Ticks::Elapsed(testCase.now, testCase.since, testCase.interval) != testCase.expected) {
			return false;
		}
	}

	return true;
}


static_assert(AllReachedHold());
static_assert(AllElapsedHold());


static_assert(Ticks::Reached(uint64{0}, uint64{0}));
static_assert(Ticks::Reached(uint64{1} << 40, (uint64{1} << 40) - 1));
static_assert(!Ticks::Reached(uint64{0}, uint64{1} << 40));
static_assert(Ticks::Reached(uint64{0} - 1, uint64{0}));
static_assert(!Ticks::Reached(uint64{0}, uint64{0} - 1));
static_assert(Ticks::Elapsed(uint64{100}, uint64{40}, uint64{60}));
static_assert(!Ticks::Elapsed(uint64{100}, uint64{40}, uint64{61}));
static_assert(!Ticks::Elapsed(uint64{10}, uint64{20}, uint64{0}));
static_assert(Ticks::Elapsed(uint64{0} - 1, uint64{0}, uint64{0} - 1));
static_assert(!Ticks::Elapsed(uint64{0} - 2, uint64{0}, uint64{0} - 1));


static_assert(Ticks::usPerMs == 1000);


static_assert(Ticks::SplitMs(0u).seconds == 0);
static_assert(Ticks::SplitMs(0u).microseconds == 0);
static_assert(Ticks::SplitMs(999u).seconds == 0);
static_assert(Ticks::SplitMs(999u).microseconds == 999000);
static_assert(Ticks::SplitMs(1000u).seconds == 1);
static_assert(Ticks::SplitMs(1000u).microseconds == 0);
static_assert(Ticks::SplitMs(1999u).seconds == 1);
static_assert(Ticks::SplitMs(1999u).microseconds == 999000);
static_assert(Ticks::SplitMs(maxTick).seconds == 4294967);
static_assert(Ticks::SplitMs(maxTick).microseconds == 295000);


int main() {
	for (ReachedCase testCase : reachedCases) {
		VHAL_CHECK_EQ(Ticks::Reached(testCase.now, testCase.due), testCase.expected);
	}

	for (ReachedCase testCase : reachedHalfRangeCases) {
		VHAL_CHECK_EQ(Ticks::Reached(testCase.now, testCase.due), testCase.expected);
	}

	for (ElapsedCase testCase : elapsedCases) {
		VHAL_CHECK_EQ(Ticks::Elapsed(testCase.now, testCase.since, testCase.interval), testCase.expected);
	}

	VHAL_CHECK(Ticks::Reached(uint64{0} - 1, uint64{0}));
	VHAL_CHECK(!Ticks::Reached(uint64{0}, uint64{0} - 1));
	VHAL_CHECK(Ticks::Elapsed(uint64{0} - 1, uint64{0}, uint64{0} - 1));
	VHAL_CHECK(!Ticks::Elapsed(uint64{10}, uint64{20}, uint64{0}));

	VHAL_CHECK_EQ(Ticks::SplitMs(maxTick).seconds, 4294967u);
	VHAL_CHECK_EQ(Ticks::SplitMs(maxTick).microseconds, 295000u);
	VHAL_CHECK_EQ(Ticks::SplitMs(1999u).microseconds, 999000u);

	return VhalCheck::Finish("TestTicks");
}
