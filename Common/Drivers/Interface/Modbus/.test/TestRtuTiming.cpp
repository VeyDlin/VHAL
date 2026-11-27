#include "SimCheck.h"
#include <Drivers/Interface/Modbus/RTU/RtuTiming.h>


// The gaps are 16.5 and 38.5 bit times, truncated to microseconds. Expected values below are worked out from that rule by hand.
constexpr RtuTiming At9600 = RtuTimingOf(9600);
static_assert(At9600.interFrameUs == 4010);
static_assert(At9600.interCharacterUs == 1718);


void TestAt9600() {
	// 385 * 100000 / 9600 = 4010.4 and 165 * 100000 / 9600 = 1718.75
	RtuTiming timing = RtuTimingOf(9600);
	SIM_EXPECT(timing.interFrameUs == 4010);
	SIM_EXPECT(timing.interCharacterUs == 1718);
}


void TestAt300() {
	// 38 500 000 / 300 = 128 333.3, and 16 500 000 / 300 = 55 000. The FreeMODBUS 16-bit count would overflow here.
	RtuTiming timing = RtuTimingOf(300);
	SIM_EXPECT(timing.interFrameUs == 128333);
	SIM_EXPECT(timing.interCharacterUs == 55000);
}


void TestFixedAboveLimit() {
	RtuTiming fast = RtuTimingOf(115200);
	SIM_EXPECT(fast.interFrameUs == 1750);
	SIM_EXPECT(fast.interCharacterUs == 750);

	RtuTiming justAbove = RtuTimingOf(19201);
	SIM_EXPECT(justAbove.interFrameUs == 1750);
	SIM_EXPECT(justAbove.interCharacterUs == 750);

	// At the limit itself the formula still applies: 38 500 000 / 19200 = 2005.2
	RtuTiming atLimit = RtuTimingOf(19200);
	SIM_EXPECT(atLimit.interFrameUs == 2005);
	SIM_EXPECT(atLimit.interCharacterUs == 859);
}


void TestCharacters() {
	// Eleven bits at 9600 is 1145.8 us per character, so two characters are 2291 us.
	SIM_EXPECT(RtuCharactersUs(9600, 1) == 1145);
	SIM_EXPECT(RtuCharactersUs(9600, 2) == 2291);
	SIM_EXPECT(RtuCharactersUs(9600, 0) == 0);
	SIM_EXPECT(RtuCharactersUs(300, 3) == 110000);
}


int main() {
	TestAt9600();
	TestAt300();
	TestFixedAboveLimit();
	TestCharacters();
	return SimFinish("TestRtuTiming");
}
