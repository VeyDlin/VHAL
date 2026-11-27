#include <VHAL.h>
#include "NcmTestDevices.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <array>
#include <cstddef>
#include <span>


// Which parameters survive a data-path session. The host sets the NTB input size, the datagram limit and the packet filter, and they keep
// their values across alternate 1: the host sets them on alternate 0 before it opens the bulk pair (Linux cdc_ncm does), and selecting
// alternate 1 must not discard them. They go back to the defaults when the host selects alternate 0, and on a configuration, a
// deconfiguration and a bus reset. The defaults of the WideFunction fixture are 4096 bytes, 0 datagrams and filter 0. The expected bytes
// are written here from NCM 1.0 section 6 and ECM 1.2 section 6.2.
namespace {


// GET_NTB_INPUT_SIZE: the size, the datagram count and the reserved word, 8 bytes.
void ExpectInputSize(WideNcmFixture& fixture, uint32 size, uint16 datagrams) {
	std::array<uint8, 8> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0xA1, 0x85, 0x0000, 0x0000, 8), std::span<const uint8>(), in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 8);
	SIM_EXPECT_EQ(in[0], static_cast<uint8>(size & 0xFF));
	SIM_EXPECT_EQ(in[1], static_cast<uint8>((size >> 8) & 0xFF));
	SIM_EXPECT_EQ(in[2], static_cast<uint8>((size >> 16) & 0xFF));
	SIM_EXPECT_EQ(in[3], static_cast<uint8>((size >> 24) & 0xFF));
	SIM_EXPECT_EQ(in[4], static_cast<uint8>(datagrams & 0xFF));
	SIM_EXPECT_EQ(in[5], static_cast<uint8>((datagrams >> 8) & 0xFF));
	SIM_EXPECT_EQ(in[6], 0x00);
	SIM_EXPECT_EQ(in[7], 0x00);
}


// SET_NTB_INPUT_SIZE with the 8-byte form: 2048 bytes and 10 datagrams, which differ from the defaults in both fields.
ResultStatus SetLoweredInput(WideNcmFixture& fixture) {
	const uint8 data[8] = {0x00, 0x08, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00};
	uint32 received = 0;
	return SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x86, 0x0000, 0x0000, 8), std::span<const uint8>(data), std::span<uint8>(), received);
}


// SET_ETHERNET_PACKET_FILTER with a bitmap of 0x0C.
ResultStatus SetFilter(WideNcmFixture& fixture) {
	return SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x43, 0x000C, 0x0000, 0));
}


// A new configuration without a bus reset: SET_CONFIGURATION(1) after SET_CONFIGURATION(0). The deconfiguration resets the values, and
// the device answers no class request while it is unconfigured, so the reset is seen through the configuration that follows it.
ResultStatus Reconfigure(WideNcmFixture& fixture) {
	return SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0));
}


void OpenWide(WideNcmFixture& fixture) {
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SelectAlternate(0), ResultStatus::ok);
}


// Set on alternate 0, the input size and datagram limit are still there after alternate 1 selects the path open.
void TestInputSizeKeptByAlternate1() {
	WideNcmFixture fixture;
	OpenWide(fixture);
	SIM_EXPECT_EQ(SetLoweredInput(fixture), ResultStatus::ok);

	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	ExpectInputSize(fixture, 2048, 10);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Set while the path is open, the values hold through a second alternate 1, which restarts the session.
void TestInputSizeSetOnAlternate1Kept() {
	WideNcmFixture fixture;
	OpenWide(fixture);
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	SIM_EXPECT_EQ(SetLoweredInput(fixture), ResultStatus::ok);

	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	ExpectInputSize(fixture, 2048, 10);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Alternate 0 selects the defaults: 4096 bytes and no datagram limit.
void TestInputSizeResetByAlternate0() {
	WideNcmFixture fixture;
	OpenWide(fixture);
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	SIM_EXPECT_EQ(SetLoweredInput(fixture), ResultStatus::ok);

	SIM_EXPECT_EQ(fixture.SelectAlternate(0), ResultStatus::ok);
	ExpectInputSize(fixture, 4096, 0);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A new configuration resets the input size.
void TestInputSizeResetByConfiguration() {
	WideNcmFixture fixture;
	OpenWide(fixture);
	SIM_EXPECT_EQ(SetLoweredInput(fixture), ResultStatus::ok);

	SIM_EXPECT_EQ(fixture.Deconfigure(), ResultStatus::ok);
	SIM_EXPECT_EQ(Reconfigure(fixture), ResultStatus::ok);
	ExpectInputSize(fixture, 4096, 0);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A bus reset resets the input size.
void TestInputSizeResetByBusReset() {
	WideNcmFixture fixture;
	OpenWide(fixture);
	SIM_EXPECT_EQ(SetLoweredInput(fixture), ResultStatus::ok);

	fixture.sim.InjectBusReset();
	fixture.Enumerate();
	ExpectInputSize(fixture, 4096, 0);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The packet filter is stored with no data stage and kept across alternate 1; alternate 0 and a configuration reset it to 0.
void TestPacketFilterLifetime() {
	WideNcmFixture fixture;
	OpenWide(fixture);
	SIM_EXPECT_EQ(SetFilter(fixture), ResultStatus::ok);

	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.function.GetPacketFilter(), 0x0C);

	SIM_EXPECT_EQ(fixture.SelectAlternate(0), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.function.GetPacketFilter(), 0x00);

	SIM_EXPECT_EQ(SetFilter(fixture), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.Deconfigure(), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.function.GetPacketFilter(), 0x00);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


}


int main() {
	TestInputSizeKeptByAlternate1();
	TestInputSizeSetOnAlternate1Kept();
	TestInputSizeResetByAlternate0();
	TestInputSizeResetByConfiguration();
	TestInputSizeResetByBusReset();
	TestPacketFilterLifetime();
	return SimTest::Finish("TestNcmParameters");
}
