#include <VHAL.h>
#include "NcmTestDevices.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <Drivers/Interface/USB/Class/CDC/USBCdcProtocol.h>
#include <array>
#include <cstddef>
#include <span>


// The class requests of the communication interface through the simulated controller. The bytes are from NCM 1.0 section 6 (table 6-3
// for the NTB parameter structure) and ECM 1.2 (the packet filter). A request this function does not implement is stalled, and so is
// a request with the wrong direction or length, or one addressed to the data interface. A stalled request leaves the value it would
// change as it was.
namespace {


// Runs an IN control request and returns its status. received is the number of bytes the device sent.
ResultStatus ControlIn(NcmFixture& fixture, const USBSetupBytes& setup, std::span<uint8> in, uint32& received) {
	return SimHost::Control(fixture.sim, setup, std::span<const uint8>(), in, received);
}


// Runs an OUT control request with a data stage of hostData.
ResultStatus ControlOut(NcmFixture& fixture, const USBSetupBytes& setup, std::span<const uint8> hostData) {
	uint32 received = 0;
	return SimHost::Control(fixture.sim, setup, hostData, std::span<uint8>(), received);
}


// GET_NTB_PARAMETERS (0x80) answers the 28-byte structure. Written by hand from table 6-3 with the defaults the function advertises:
//   wLength 28; bmNtbFormatsSupported 1 (NTB-16); dwNtbInMaxSize 2048; wNdpInDivisor 4; wNdpInPayloadRemainder 0; wNdpInAlignment 4;
//   wReserved 0; dwNtbOutMaxSize 2048; wNdpOutDivisor 4; wNdpOutPayloadRemainder 0; wNdpOutAlignment 4; wNtbOutMaxDatagrams 0.
constexpr uint8 NtbParametersBytes[] = {
	0x1C, 0x00,
	0x01, 0x00,
	0x00, 0x08, 0x00, 0x00,
	0x04, 0x00,
	0x00, 0x00,
	0x04, 0x00,
	0x00, 0x00,
	0x00, 0x08, 0x00, 0x00,
	0x04, 0x00,
	0x00, 0x00,
	0x04, 0x00,
	0x00, 0x00
};


static_assert(sizeof(NtbParametersBytes) == 28, "the NTB parameter structure is 28 bytes");


void TestNtbParameters() {
	NcmFixture fixture;
	fixture.Configure();

	std::array<uint8, 64> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(ControlIn(fixture, SimHost::MakeSetup(0xA1, 0x80, 0x0000, 0x0000, 28), in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 28);
	for (std::size_t index = 0; index < 28; index++) {
		SIM_EXPECT_EQ(in[index], NtbParametersBytes[index]);
	}

	// The structure is an IN answer only. An OUT request for it is stalled.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x80, 0x0000, 0x0000, 0)), ResultStatus::accessError);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The NTB format: GET answers 0 (NTB-16), SET of 0 is accepted, SET of 1 (NTB-32) is stalled and the format stays NTB-16.
void TestNtbFormat() {
	NcmFixture fixture;
	fixture.Configure();

	std::array<uint8, 2> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(ControlIn(fixture, SimHost::MakeSetup(0xA1, 0x83, 0x0000, 0x0000, 2), in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 2);
	SIM_EXPECT_EQ(in[0], 0x00);
	SIM_EXPECT_EQ(in[1], 0x00);

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x84, 0x0001, 0x0000, 0)), ResultStatus::accessError);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x84, 0x0000, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The NTB input size: the default is dwNtbInMaxSize (2048). Both forms are accepted: 4 bytes, and 8 bytes with the datagram count. A
// size below 2048 or above dwNtbInMaxSize is stalled and the size stays as it was.
void TestNtbInputSize() {
	NcmFixture fixture;
	fixture.Configure();

	std::array<uint8, 8> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(ControlIn(fixture, SimHost::MakeSetup(0xA1, 0x85, 0x0000, 0x0000, 8), in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 8);
	const uint8 defaultSize[8] = {0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	for (std::size_t index = 0; index < 8; index++) {
		SIM_EXPECT_EQ(in[index], defaultSize[index]);
	}

	// 1000 bytes is below 2048 (0x000003E8), so it is refused: E8 03 00 00.
	const uint8 tooSmall[4] = {0xE8, 0x03, 0x00, 0x00};
	SIM_EXPECT_EQ(ControlOut(fixture, SimHost::MakeSetup(0x21, 0x86, 0x0000, 0x0000, 4), tooSmall), ResultStatus::accessError);

	// 4096 bytes is above dwNtbInMaxSize (2048): 00 10 00 00.
	const uint8 tooLarge[4] = {0x00, 0x10, 0x00, 0x00};
	SIM_EXPECT_EQ(ControlOut(fixture, SimHost::MakeSetup(0x21, 0x86, 0x0000, 0x0000, 4), tooLarge), ResultStatus::accessError);

	// The 8-byte form with 1000 bytes is refused too, whatever the datagram count after it.
	const uint8 tooSmallLong[8] = {0xE8, 0x03, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00};
	SIM_EXPECT_EQ(ControlOut(fixture, SimHost::MakeSetup(0x21, 0x86, 0x0000, 0x0000, 8), tooSmallLong), ResultStatus::accessError);

	// The size is unchanged by the refusals.
	SIM_EXPECT_EQ(ControlIn(fixture, SimHost::MakeSetup(0xA1, 0x85, 0x0000, 0x0000, 4), in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(in[0], 0x00);
	SIM_EXPECT_EQ(in[1], 0x08);
	SIM_EXPECT_EQ(in[2], 0x00);
	SIM_EXPECT_EQ(in[3], 0x00);

	// 2048 bytes in the 4-byte form (00 08 00 00) is accepted.
	const uint8 minimum[4] = {0x00, 0x08, 0x00, 0x00};
	SIM_EXPECT_EQ(ControlOut(fixture, SimHost::MakeSetup(0x21, 0x86, 0x0000, 0x0000, 4), minimum), ResultStatus::ok);

	// The 8-byte form: 2048 bytes and 10 datagrams (0x0A). The GET answers the same 8 bytes.
	const uint8 withDatagrams[8] = {0x00, 0x08, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00};
	SIM_EXPECT_EQ(ControlOut(fixture, SimHost::MakeSetup(0x21, 0x86, 0x0000, 0x0000, 8), withDatagrams), ResultStatus::ok);
	SIM_EXPECT_EQ(ControlIn(fixture, SimHost::MakeSetup(0xA1, 0x85, 0x0000, 0x0000, 8), in, received), ResultStatus::ok);
	for (std::size_t index = 0; index < 8; index++) {
		SIM_EXPECT_EQ(in[index], withDatagrams[index]);
	}

	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The packet filter (ECM 1.2 table 6): SET_ETHERNET_PACKET_FILTER with the bitmap in wValue is stored and acknowledged, with no data.
void TestPacketFilter() {
	NcmFixture fixture;
	fixture.Configure();

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x43, 0x000C, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.function.GetPacketFilter(), 0x0C);

	// A packet filter with a data stage is not the request: it is stalled.
	const uint8 data[2] = {0x01, 0x00};
	SIM_EXPECT_EQ(ControlOut(fixture, SimHost::MakeSetup(0x21, 0x43, 0x0001, 0x0000, 2), data), ResultStatus::accessError);
	SIM_EXPECT_EQ(fixture.function.GetPacketFilter(), 0x0C);
}


// The requests the function does not implement are stalled: the multicast filters, the statistics, the net address, the CRC mode,
// the maximum datagram size and the encapsulated command. The data interface's requests are stalled too.
void TestUnsupportedRequestsStall() {
	NcmFixture fixture;
	fixture.Configure();

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x40, 0x0000, 0x0000, 0)), ResultStatus::accessError);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0xA1, 0x44, 0x0000, 0x0000, 4)), ResultStatus::accessError);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0xA1, 0x81, 0x0000, 0x0000, 6)), ResultStatus::accessError);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x82, 0x0000, 0x0000, 0)), ResultStatus::accessError);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0xA1, 0x89, 0x0000, 0x0000, 2)), ResultStatus::accessError);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x8A, 0x0000, 0x0000, 0)), ResultStatus::accessError);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0xA1, 0x87, 0x0000, 0x0000, 2)), ResultStatus::accessError);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x88, 0x0000, 0x0000, 0)), ResultStatus::accessError);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x00, 0x0000, 0x0000, 0)), ResultStatus::accessError);

	// A request to the data interface (wIndex 1) is not one of the communication interface's requests.
	std::array<uint8, 2> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(ControlIn(fixture, SimHost::MakeSetup(0xA1, 0x83, 0x0000, 0x0001, 2), in, received), ResultStatus::accessError);

	// The same request to the device recipient (bmRequestType 0x20) has no owner in the device, so it is stalled.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x20, 0x43, 0x0000, 0x0000, 0)), ResultStatus::accessError);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The data and the length of the SET requests: SET_NTB_FORMAT and SET_ETHERNET_PACKET_FILTER take no data, GET_NTB_FORMAT answers 2
// bytes and an IN request with no length is stalled.
void TestLengthsAreChecked() {
	NcmFixture fixture;
	fixture.Configure();

	const uint8 data[2] = {0x00, 0x00};
	SIM_EXPECT_EQ(ControlOut(fixture, SimHost::MakeSetup(0x21, 0x84, 0x0000, 0x0000, 2), data), ResultStatus::accessError);

	std::array<uint8, 2> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(ControlIn(fixture, SimHost::MakeSetup(0xA1, 0x83, 0x0000, 0x0000, 0), in, received), ResultStatus::accessError);

	// SET_NTB_INPUT_SIZE takes 4 or 8 bytes; a 6-byte stage is stalled.
	const uint8 six[6] = {0x00, 0x08, 0x00, 0x00, 0x00, 0x00};
	SIM_EXPECT_EQ(ControlOut(fixture, SimHost::MakeSetup(0x21, 0x86, 0x0000, 0x0000, 6), six), ResultStatus::accessError);
}


}


int main() {
	TestNtbParameters();
	TestNtbFormat();
	TestNtbInputSize();
	TestPacketFilter();
	TestUnsupportedRequestsStall();
	TestLengthsAreChecked();
	return SimTest::Finish("TestNcmRequests");
}
