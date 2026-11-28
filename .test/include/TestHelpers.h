#pragma once
// Helpers that the host tests of several modules share: the bytes of a text, and the simulated clock at the start of a test case.
// The System functions they use come from the shim of the test frame.
#include <VhalSystem.h>

#include <cstdint>
#include <string_view>
#include <vector>


namespace TestHelpers {


// The bytes of a text, without a terminating zero.
inline std::vector<uint8> AsBytes(std::string_view text) {
	return std::vector<uint8>(text.begin(), text.end());
}


// Puts the simulated clock at 1000 ms and clears the record of System::Reset: the state that a test case starts from.
inline void ResetSystem() {
	VhalSim::SetMs(1000);
	VhalSim::resetCalled = false;
}


}
