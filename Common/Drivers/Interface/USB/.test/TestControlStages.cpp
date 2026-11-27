#include <VHAL.h>
#include "DeviceFixture.h"
#include "Descriptors.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <array>
#include <span>

// Control transfer stages: a SETUP that interrupts a data stage, a SET_ADDRESS that a SETUP abandons, and the zero-length
// packet rule. Endpoint 0 stages follow USB 2.0 section 8.6.2: a SETUP is DATA0, and each data and status stage starts at
// DATA1. The toggle of endpoint 0 IN shows where a stage ended: each packet flips it, so a stage of N packets from DATA1
// leaves it at DATA1 when N is even and at DATA0 when N is odd. A zero-length packet is one of those packets.


namespace {


void TestSetupInterruptsInDataStage() {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	fixture.sim.InjectBusReset();

	// Start a 32-byte configuration answer, then interrupt it before the host reads any of it. The SETUP is DATA0, and the
	// data stage it arms starts at DATA1.
	fixture.sim.InjectSetup(SimHost::MakeSetup(0x80, 6, 0x0200, 0x0000, 255));
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x80));
	SIM_EXPECT(fixture.sim.GetDataToggle(USBEndpoint::ControlIn));
	fixture.sim.InjectSetup(SimHost::MakeSetup(0x80, 6, 0x0100, 0x0000, 18));
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x80));
	SIM_EXPECT(fixture.sim.GetDataToggle(USBEndpoint::ControlIn));

	// The host reads the device descriptor, not the configuration that was abandoned.
	std::array<uint8, 64> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(USBEndpoint::ControlIn, in, received), ResultStatus::ok);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(received), SimDescriptors::DeviceDescriptor));
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x00));
	SIM_EXPECT(fixture.sim.HostOut(0x00, std::span<const uint8>()).IsOk());
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x00));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
	SIM_EXPECT_EQ(fixture.sim.GetToggleMismatchCount(), 0);
}


void TestSetupInterruptsOutDataStage() {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	fixture.sim.InjectBusReset();

	// The interface of a class request exists only once the device is configured (USB 2.0 section 9.4), so before this
	// SET_CONFIGURATION the core stalls the request. The device is configured first, so the stage under test is the OUT data
	// stage of a configured device. The explicit stall before configuration is checked in TestStateRules.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Configured);

	// A class OUT request with a 2-byte data stage. The function receives its buffer and the core arms the OUT transfer.
	fixture.sim.InjectSetup(SimHost::MakeSetup(0x21, 0x20, 0x0000, 0x0000, 2));
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x00));
	SIM_EXPECT(fixture.sim.GetDataToggle(USBEndpoint::ControlOut));

	// Before the host sends the data, a GET_STATUS arrives. It abandons the OUT stage with no completion event.
	fixture.sim.InjectSetup(SimHost::MakeSetup(0x80, 0, 0x0000, 0x0000, 2));
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x00));
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x80));

	// The OUT endpoint is no longer armed, so the host's late data is NAKed and nothing reaches the function.
	std::array<uint8, 2> late = { 0x12, 0x34 };
	SIM_EXPECT_EQ(fixture.sim.HostOut(0x00, late).Error(), ResultStatus::notReady);

	std::array<uint8, 2> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(USBEndpoint::ControlIn, in, received), ResultStatus::ok);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(received), std::array<uint8, 2>{ 0x00, 0x00 }));
	SIM_EXPECT(fixture.sim.HostOut(0x00, std::span<const uint8>()).IsOk());

	SIM_EXPECT(fixture.function.controlOutData.count == 0);
	SIM_EXPECT_EQ(fixture.function.requests.count, 1);
	SIM_EXPECT_EQ(fixture.function.requests[0], TestFunction::ClassOutRequest);
	// The abandoned stage leaves the state as it was: the device is still configured.
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Configured);
	SIM_EXPECT_EQ(fixture.sim.GetToggleMismatchCount(), 0);
}


void TestSetAddressAbandonedBySetup() {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	fixture.sim.InjectBusReset();

	// SET_ADDRESS 5 is answered with its status stage, which the host has not taken yet.
	fixture.sim.InjectSetup(SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0));
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x80));
	SIM_EXPECT_EQ(fixture.sim.GetAddress(), 0);

	// A GET_DESCRIPTOR before that status abandons the status and drops the pending address.
	fixture.sim.InjectSetup(SimHost::MakeSetup(0x80, 6, 0x0100, 0x0000, 18));
	std::array<uint8, 64> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(USBEndpoint::ControlIn, in, received), ResultStatus::ok);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(received), SimDescriptors::DeviceDescriptor));
	SIM_EXPECT(fixture.sim.HostOut(0x00, std::span<const uint8>()).IsOk());
	SIM_EXPECT_EQ(fixture.sim.GetAddress(), 0);
	SIM_EXPECT_EQ(fixture.device.GetAddress(), 0);
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Default);

	// SET_ADDRESS completed in full this time, so the address is applied at the status.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.sim.GetAddress(), 5);
	SIM_EXPECT_EQ(fixture.device.GetAddress(), 5);
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Address);
	SIM_EXPECT_EQ(fixture.sim.GetToggleMismatchCount(), 0);
}


// Runs one IN request on endpoint 0 and returns the toggle of the next packet afterwards. expected is what the host must
// receive.
bool ToggleAfterInRequest(DeviceFixture& fixture, const USBSetupBytes& setup, std::span<const uint8> expected) {
	std::array<uint8, 256> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, setup, std::span<const uint8>(), in, received), ResultStatus::ok);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(received), expected));
	return fixture.sim.GetDataToggle(USBEndpoint::ControlIn);
}


void TestZeroLengthPacketRule() {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	fixture.sim.InjectBusReset();

	// Answer 64 bytes to a request for 128: shorter than wLength and a multiple of 64, so a zero-length packet follows.
	// That is two packets from DATA1, which leaves the toggle at DATA1.
	SIM_EXPECT(ToggleAfterInRequest(fixture, SimHost::MakeSetup(0xC0, 0x50, 0x0000, 0x0000, 128), SimDescriptors::VendorBlob64));

	// Answer exactly wLength (64 for 64): no zero-length packet, one packet from DATA1, which leaves the toggle at DATA0.
	SIM_EXPECT(!ToggleAfterInRequest(fixture, SimHost::MakeSetup(0xC0, 0x50, 0x0000, 0x0000, 64), SimDescriptors::VendorBlob64));

	// Answer 32 bytes to a request for 255: shorter than wLength, not a multiple of 64, so the short packet ends it.
	SIM_EXPECT(!ToggleAfterInRequest(fixture, SimHost::MakeSetup(0x80, 6, 0x0200, 0x0000, 255), SimDescriptors::ConfigurationDescriptor));

	// Answer 18 bytes to a request for 64: the same short-packet end, a different length.
	SIM_EXPECT(!ToggleAfterInRequest(fixture, SimHost::MakeSetup(0x80, 6, 0x0100, 0x0000, 64), SimDescriptors::DeviceDescriptor));

	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
	SIM_EXPECT_EQ(fixture.sim.GetToggleMismatchCount(), 0);
}


// A device suspended from Configured answers its configuration requests as configured, the state it returns to on resume. A
// SET_CONFIGURATION(0) that arrives while suspended is accepted, and resume then returns the device to Address.
void TestConfiguredRequestsAnsweredWhileSuspended() {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	fixture.sim.InjectBusReset();
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Configured);

	fixture.sim.InjectSuspend();
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Suspended);

	// GET_CONFIGURATION answers 1 and GET_INTERFACE answers alternate setting 0, the same as when not suspended.
	std::array<uint8, 1> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x80, 8, 0x0000, 0x0000, 1), std::span<const uint8>(), in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 1);
	SIM_EXPECT_EQ(in[0], 1);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x81, 10, 0x0000, 0x0000, 1), std::span<const uint8>(), in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 1);
	SIM_EXPECT_EQ(in[0], 0);

	// SET_CONFIGURATION(0) is accepted while suspended. The device stays Suspended and resumes as Address.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 0, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Suspended);
	SIM_EXPECT_EQ(fixture.device.GetConfigurationValue(), 0);
	fixture.sim.InjectResume();
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Address);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
	SIM_EXPECT_EQ(fixture.sim.GetToggleMismatchCount(), 0);
}


}


int main() {
	TestSetupInterruptsInDataStage();
	TestSetupInterruptsOutDataStage();
	TestSetAddressAbandonedBySetup();
	TestZeroLengthPacketRule();
	TestConfiguredRequestsAnsweredWhileSuspended();
	return SimTest::Finish("TestControlStages");
}
