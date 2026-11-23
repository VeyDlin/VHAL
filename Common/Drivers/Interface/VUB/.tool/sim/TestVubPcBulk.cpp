// libs-windows: winusb setupapi cfgmgr32
// libs-linux: usb-1.0
#include "SimCheck.h"
#include <Drivers/Interface/VUB/Blocks.h>
#include <Drivers/Interface/VUB/PC/BulkLink.h>
#include <Drivers/Interface/VUB/Receiver.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <string>


// The vendor bulk link is compiled on both systems and its concepts are checked here. No test opens a device: enumerating or opening a
// USB device is not run in a test. The pure parts (the serial match, and on Windows the GUID text) are checked as plain functions.


using Bulk = VUB::PC::BulkLink;


static_assert(VUB::StreamLink<Bulk>, "the bulk link is a byte stream: Write, no Send");
static_assert(!VUB::PacketLink<Bulk>, "the bulk link is not a packet link");
static_assert(VUB::PollingLink<Bulk>, "the bulk link is polled from Process");
static_assert(VUB::ReportingLink<Bulk>, "the bulk link reports its state");
static_assert(!VUB::ReceivingLink<Bulk>, "the bulk link does not receive in an interrupt");
static_assert(Bulk::traits.reliable, "the bulk link is reliable");
static_assert(Bulk::traits.reportsState, "the bulk link reports its state");


inline void CheckSerialMatch() {
	SIM_EXPECT(Bulk::SerialMatches("", "any"));
	SIM_EXPECT(Bulk::SerialMatches("", ""));
	SIM_EXPECT(Bulk::SerialMatches("A1B2", "A1B2"));
	SIM_EXPECT(!Bulk::SerialMatches("A1B2", "A1B3"));
	SIM_EXPECT(!Bulk::SerialMatches("A1B2", ""));
}


#if defined(_WIN32)
inline void CheckGuidText() {
	GUID guid{};
	SIM_EXPECT(Bulk::ParseGuid("{53B72DEF-1477-422A-B645-D5267E3E44A6}", guid));
	SIM_EXPECT(guid.Data1 == 0x53B72DEF);
	SIM_EXPECT(guid.Data2 == 0x1477);
	SIM_EXPECT(guid.Data3 == 0x422A);
	SIM_EXPECT(guid.Data4[0] == 0xB6);
	SIM_EXPECT(guid.Data4[7] == 0xA6);
	SIM_EXPECT(Bulk::ParseGuid("53B72DEF147722AB645D5267E3E44A6", guid) == false);
	SIM_EXPECT(!Bulk::ParseGuid("{xyz}", guid));
	SIM_EXPECT(!Bulk::ParseGuid("", guid));
}
#endif


// A link that is not open takes nothing and polls nothing.
inline void CheckClosedLink() {
	Bulk link;
	SIM_EXPECT(!link.IsOpen());
	const std::array<uint8, 4> bytes = {1, 2, 3, 4};
	SIM_EXPECT(link.Write(bytes).Value() == 0);
	link.Poll(VUB::Receiver());
	SIM_EXPECT(!link.IsOpen());
}


int main() {
	CheckSerialMatch();
#if defined(_WIN32)
	CheckGuidText();
#endif
	CheckClosedLink();
	return SimFinish("TestVubPcBulk");
}
