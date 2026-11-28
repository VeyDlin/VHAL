#include <Utilities/Time/Ticks.h>
#include <VhalCheck.h>


// The examples of the Ticks wiki page, as the page writes them, each in its own namespace so that they compile as written. A check
// follows each example with the value the page states.


namespace ReachedExample {
	uint32 deadline = 0xFFFFFFF0u;

	bool late = Ticks::Reached(0x00000010u, deadline);
	bool early = Ticks::Reached(0xFFFFFFF0u, 0x00000010u);
}


namespace ElapsedExample {
	uint32 start = 0xFFFFFFFCu;
	uint32 nowTick = 0x00000004u;

	bool done = Ticks::Elapsed(nowTick, start, 8);
	bool notYet = Ticks::Elapsed(nowTick, start, 9);
}


namespace SixtyFourBitExample {
	uint64 totalUs = 5000000;

	bool reached = Ticks::Reached(totalUs, uint64{4000000});
	bool elapsed = Ticks::Elapsed(totalUs, uint64{4000000}, uint64{1000000});
}


namespace SplitExample {
	Ticks::Split parts = Ticks::SplitMs(2500);

	uint32 delayUs = Ticks::usPerMs * 20;
}


int main() {
	VHAL_CHECK(ReachedExample::late);
	VHAL_CHECK(!ReachedExample::early);

	VHAL_CHECK(ElapsedExample::done);
	VHAL_CHECK(!ElapsedExample::notYet);

	VHAL_CHECK(SixtyFourBitExample::reached);
	VHAL_CHECK(SixtyFourBitExample::elapsed);

	VHAL_CHECK_EQ(SplitExample::parts.seconds, 2u);
	VHAL_CHECK_EQ(SplitExample::parts.microseconds, 500000u);
	VHAL_CHECK_EQ(SplitExample::delayUs, 20000u);

	return VhalCheck::Finish("TestTicksReadme");
}
