#include <VHAL.h>
#include <VhalCheck.h>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>


// Self-test of the check header and of the shared shim. Every macro runs, every accepted type is compared, and the shim's clock and
// critical-section count are checked. Every check here holds: a failing check would make the test fail.


namespace {


enum class Mode : std::uint8_t {
	idle = 1,
	running = 2
};


enum Plain {
	plainOne = 1
};


}


int main() {
	// VHAL_CHECK
	VHAL_CHECK(1 + 1 == 2);
	VHAL_CHECK(!false);

	// Integers of every signedness
	VHAL_CHECK_EQ(3, 3);
	VHAL_CHECK_EQ(-5, -5LL);
	VHAL_CHECK_EQ(std::uint32_t{7}, 7);
	VHAL_CHECK_EQ(std::uint64_t{0xFFFFFFFFFFFFFFFFu}, std::numeric_limits<std::uint64_t>::max());
	VHAL_CHECK_EQ(std::int64_t{std::numeric_limits<std::int64_t>::min()}, std::numeric_limits<std::int64_t>::min());
	VHAL_CHECK_EQ(static_cast<std::uint8_t>(255), 255);
	VHAL_CHECK_EQ('a', 97);

	// Enums compare by their underlying value
	VHAL_CHECK_EQ(Mode::running, 2);
	VHAL_CHECK_EQ(Mode::idle, Mode::idle);
	VHAL_CHECK_EQ(plainOne, 1);

	// bool
	VHAL_CHECK_EQ(true, 1 < 2);
	VHAL_CHECK_EQ(false, 2 < 1);

	// Text: string literals, const char* and std::string_view. The text of a std::string converts too.
	const char* name = "frame";
	std::string_view view = "frame";
	std::string owned = "frame";
	VHAL_CHECK_EQ("frame", std::string_view("frame"));
	VHAL_CHECK_EQ(name, "frame");
	VHAL_CHECK_EQ(view, name);
	VHAL_CHECK_EQ(owned, view);

	// VHAL_CHECK_NEAR, with integers, float and double
	VHAL_CHECK_NEAR(0.1 + 0.2, 0.3, 1e-9);
	VHAL_CHECK_NEAR(5, 5.0f, 0.0);
	VHAL_CHECK_NEAR(1.0f, 1.25f, 0.25);
	VHAL_CHECK_NEAR(-2.5, -2.0, 0.5);

	// The shim's clock: SetMs, AdvanceMs, GetMs, GetTick, GetTick32, DelayMs and DelayUs
	VhalSim::SetMs(1000);
	VHAL_CHECK_EQ(System::GetMs(), 1000u);
	VHAL_CHECK_EQ(System::GetTick(), 1000u);
	VHAL_CHECK_EQ(System::GetTick32(), 1000u);

	VhalSim::AdvanceMs(250);
	VHAL_CHECK_EQ(System::GetMs(), 1250u);

	System::DelayMs(40);
	VHAL_CHECK_EQ(System::GetMs(), 1290u);

	System::DelayUs(2000);
	VHAL_CHECK_EQ(System::GetMs(), 1292u);

	System::DelayUs(1);
	VHAL_CHECK_EQ(System::GetMs(), 1293u);

	System::DelayUs(0);
	VHAL_CHECK_EQ(System::GetMs(), 1293u);

	VhalSim::SetMs(0x100000005ull);
	VHAL_CHECK_EQ(System::GetMs(), 0x100000005ull);
	VHAL_CHECK_EQ(System::GetTick32(), 5u);

	VHAL_CHECK_EQ(System::IsInterrupt(), false);

	// The shim's critical sections: depth and enters
	const std::uint32_t enters = VhalSim::CriticalEnters();
	VHAL_CHECK_EQ(VhalSim::CriticalDepth(), 0);

	System::CriticalSection(true);
	VHAL_CHECK_EQ(VhalSim::CriticalDepth(), 1);

	System::CriticalSection(true);
	VHAL_CHECK_EQ(VhalSim::CriticalDepth(), 2);

	System::CriticalSection(false);
	System::CriticalSection(false);
	VHAL_CHECK_EQ(VhalSim::CriticalDepth(), 0);
	VHAL_CHECK_EQ(VhalSim::CriticalEnters() - enters, 2u);

	return VhalCheck::Finish("TestFrameChecks");
}
