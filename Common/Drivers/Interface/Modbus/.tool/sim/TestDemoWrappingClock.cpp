#include "SimCheck.h"
#include "../../../../../../.demo/STM32G4/Application/DemoModbus/WrappingClock.h"
#include <cstdint>


// The DWT cycle counter wraps every 2^32 cycles. At 170 cycles per microsecond the microseconds must keep rising across the wrap,
// because the transport measures gaps as unsigned differences of the clock.
constexpr std::uint32_t CyclesPerUs = 170;


void TestNoWrap() {
	WrappingClock clock;
	SIM_EXPECT(clock.Microseconds(1700, CyclesPerUs) == 10);
	SIM_EXPECT(clock.Microseconds(3400, CyclesPerUs) == 20);
}


void TestAcrossOneWrap() {
	WrappingClock clock;
	std::uint32_t before = clock.Microseconds(0xFFFFFF00U, CyclesPerUs);
	std::uint32_t after = clock.Microseconds(0x00000100U, CyclesPerUs);

	// The true count is 2^32 + 256 cycles, so the value is ((2^32 + 256) / 170) as a uint32.
	std::uint64_t trueTotal = (static_cast<std::uint64_t>(1) << 32) + 0x100U;
	SIM_EXPECT(after == static_cast<std::uint32_t>(trueTotal / CyclesPerUs));

	// 512 cycles is about 3 microseconds, so the gap across the wrap is small, not about 25 million.
	SIM_EXPECT(static_cast<std::uint32_t>(after - before) <= 4);
}


void TestManyWraps() {
	WrappingClock clock;
	std::uint32_t cycles = 0;
	std::uint64_t total = 0;
	for (int step = 0; step < 40; step++) {
		cycles = cycles + 0x10000000U;
		total = total + 0x10000000U;
		SIM_EXPECT(clock.Microseconds(cycles, CyclesPerUs) == static_cast<std::uint32_t>(total / CyclesPerUs));
	}
}


int main() {
	TestNoWrap();
	TestAcrossOneWrap();
	TestManyWraps();
	return SimFinish("TestDemoWrappingClock");
}
