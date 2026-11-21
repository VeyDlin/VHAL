#include <VHAL.h>
#include "SimAssert.h"
#include <Drivers/Interface/USB/USBProtocol.h>
#include <string_view>


// The state names of the demos' status lines. Each enumerator has its own text, a value past the enum gives "Unknown", and the name
// is a constant expression, so a status table can use it at compile time.


namespace {


static_assert(std::string_view(USBDeviceStateName(USBDeviceState::Configured)) == "Configured", "the name is a constant expression");


// The texts are written by hand from the demos' status lines, so a changed name fails here.
void TestNamesOfStates() {
	SIM_EXPECT(std::string_view(USBDeviceStateName(USBDeviceState::Detached)) == "Detached");
	SIM_EXPECT(std::string_view(USBDeviceStateName(USBDeviceState::Default)) == "Default");
	SIM_EXPECT(std::string_view(USBDeviceStateName(USBDeviceState::Address)) == "Address");
	SIM_EXPECT(std::string_view(USBDeviceStateName(USBDeviceState::Configured)) == "Configured");
	SIM_EXPECT(std::string_view(USBDeviceStateName(USBDeviceState::Suspended)) == "Suspended");
}


// A value past the last enumerator, as a state added to the enum without a name here would be, gives "Unknown".
void TestUnknownState() {
	USBDeviceState past = static_cast<USBDeviceState>(5);
	SIM_EXPECT(std::string_view(USBDeviceStateName(past)) == "Unknown");
	SIM_EXPECT(std::string_view(USBDeviceStateName(static_cast<USBDeviceState>(0xFF))) == "Unknown");
}


}


int main() {
	TestNamesOfStates();
	TestUnknownState();
	return SimTest::Finish("TestDeviceStateName");
}
