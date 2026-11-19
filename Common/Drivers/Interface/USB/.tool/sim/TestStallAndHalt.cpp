#include <VHAL.h>
#include "DeviceFixture.h"
#include "Descriptors.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <array>
#include <span>

// Stalls and halts: requests the core does not answer stall, the next SETUP clears the stall, and an endpoint halt stops
// the transfer, reports through GET_STATUS, and clears with the data toggle back at DATA0.


namespace {


void ExpectIn(SimUSBDeviceAdapter& sim, const USBSetupBytes& setup, std::span<const uint8> expected) {
	std::array<uint8, 256> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(SimHost::Control(sim, setup, std::span<const uint8>(), in, received), ResultStatus::ok);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(received), expected));
}


void ExpectStall(SimUSBDeviceAdapter& sim, const USBSetupBytes& setup) {
	std::array<uint8, 256> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(SimHost::Control(sim, setup, std::span<const uint8>(), in, received), ResultStatus::accessError);
}


void TestUnansweredRequestsStall() {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	fixture.sim.InjectBusReset();

	// bRequest 0x33 is not a standard request, so the core stalls it. The stall halts both endpoint 0 directions.
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x80, 0x33, 0x0000, 0x0000, 8));
	SIM_EXPECT(fixture.sim.IsStalled(USBEndpoint::ControlIn));
	SIM_EXPECT(fixture.sim.IsStalled(USBEndpoint::ControlOut));
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(USBEndpoint::ControlIn), 1);

	// The next SETUP clears the stall, and the request it carries is answered.
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 6, 0x0100, 0x0000, 18), SimDescriptors::DeviceDescriptor);
	SIM_EXPECT(!fixture.sim.IsStalled(USBEndpoint::ControlIn));
	SIM_EXPECT(!fixture.sim.IsStalled(USBEndpoint::ControlOut));

	// Other requests the core does not answer: SET_DESCRIPTOR and SYNCH_FRAME on the device, vendor requests with no matching
	// entry (wrong direction or bRequest), and requests to the Other recipient (bmRequestType bits 4..0 = 3) and to a reserved
	// recipient (4 and up, which match no recipient).
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x00, 7, 0x0000, 0x0000, 0));
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x80, 12, 0x0000, 0x0000, 2));
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x83, 0x00, 0x0000, 0x0000, 2));
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x84, 0x00, 0x0000, 0x0000, 2));
	ExpectStall(fixture.sim, SimHost::MakeSetup(0xC0, 0x99, 0x0000, 0x0000, 4));
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x40, 0x42, 0x0000, 0x0007, 0));
	ExpectStall(fixture.sim, SimHost::MakeSetup(0xA0, 0x10, 0x0000, 0x0000, 4));
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 6, 0x0100, 0x0000, 18), SimDescriptors::DeviceDescriptor);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


void TestEndpointHaltAndDataToggle() {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	fixture.sim.InjectBusReset();
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0)), ResultStatus::ok);

	// One packet goes out on 0x81, which flips its toggle to DATA1.
	std::array<uint8, 64> packetA = {};
	std::array<uint8, 64> packetB = {};
	packetA.fill(0xA1);
	packetB.fill(0xB2);
	std::array<uint8, 64> in = {};
	uint32 received = 0;
	fixture.function.QueueIn(packetA);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(received), packetA));
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81));

	// The next transfer is armed and then halted by the host. The halt abandons it and the toggle stays where it was.
	fixture.function.QueueIn(packetB);
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0081, 0)), ResultStatus::ok);
	SIM_EXPECT(fixture.sim.IsStalled(0x81));
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81));
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(0x81), 1);
	SIM_EXPECT(fixture.function.haltChanges.count != 0);
	SIM_EXPECT(fixture.function.haltChanges.Back().address == 0x81 && fixture.function.haltChanges.Back().halted);

	// GET_STATUS reports the halt in bit 0 of its two bytes.
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x82, 0, 0x0000, 0x0081, 2), std::array<uint8, 2>{ 0x01, 0x00 });

	// The host's IN to a halted endpoint gets STALL.
	received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::accessError);

	// CLEAR_FEATURE(ENDPOINT_HALT) clears the halt, sets the toggle back to DATA0, and the function re-arms its queued data.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 1, 0x0000, 0x0081, 0)), ResultStatus::ok);
	SIM_EXPECT(!fixture.sim.IsStalled(0x81));
	SIM_EXPECT(!fixture.sim.GetDataToggle(0x81));
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT(fixture.function.haltChanges.Back().address == 0x81 && !fixture.function.haltChanges.Back().halted);
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x82, 0, 0x0000, 0x0081, 2), std::array<uint8, 2>{ 0x00, 0x00 });

	received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(received), packetB));
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81));
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(0x81), 1);
	SIM_EXPECT(!fixture.function.AnyOverflow());
}


void TestEndpointZeroAndUnownedHalts() {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	fixture.sim.InjectBusReset();

	// Endpoint 0 has no halt to set through this request: it is acknowledged, and nothing is halted.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0080, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT(!fixture.sim.IsStalled(0x80));
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(0x80), 0);

	// In Default the state rule stalls 0x82 first, whatever the function table says. Ownership is checked once configured, see
	// TestStateRules.
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x82, 0, 0x0000, 0x0082, 2));
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0082, 0));

	// 0x81 is owned by the function, but it is an endpoint other than 0, which does not exist before SET_CONFIGURATION (USB 2.0
	// section 9.4). GET_STATUS and SET_FEATURE(ENDPOINT_HALT) on it are request errors, so the core stalls both before they
	// reach the adapter or the function. The rule itself is checked in TestStateRules.
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x82, 0, 0x0000, 0x0081, 2));
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0081, 0));
	SIM_EXPECT(!fixture.sim.IsStalled(0x81));
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(0x81), 0);
	SIM_EXPECT_EQ(fixture.function.haltChanges.count, 0);

	// Configure the device, so the endpoint is open. GET_STATUS on it is then answered from the halt flag, which is clear.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0)), ResultStatus::ok);
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x82, 0, 0x0000, 0x0081, 2), std::array<uint8, 2>{ 0x00, 0x00 });
	SIM_EXPECT(!fixture.sim.IsStalled(0x81));

	// A feature other than ENDPOINT_HALT on an owned endpoint goes to the function, which does not answer it.
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x02, 1, 0x0001, 0x0081, 0));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);

	// The function owns 0x81 but the adapter has closed it. The core asks the adapter to halt it, the adapter answers notExist, and
	// the request stalls with no halt recorded and no function call. The adapter's own notExist is checked in TestAdapterRules.
	SIM_EXPECT_EQ(fixture.sim.CloseEndpoint(0x81), ResultStatus::ok);
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0081, 0));
	SIM_EXPECT(!fixture.sim.IsStalled(0x81));
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(0x81), 0);
	SIM_EXPECT_EQ(fixture.function.haltChanges.count, 0);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);

	// Deconfigured again, the endpoint no longer exists, and the same halt request is rejected by the state rule.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 0, 0x0000, 0)), ResultStatus::ok);
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0081, 0));
	SIM_EXPECT(!fixture.sim.IsStalled(0x81));
	SIM_EXPECT_EQ(fixture.function.haltChanges.count, 0);
}


}


int main() {
	TestUnansweredRequestsStall();
	TestEndpointHaltAndDataToggle();
	TestEndpointZeroAndUnownedHalts();
	return SimTest::Finish("TestStallAndHalt");
}
