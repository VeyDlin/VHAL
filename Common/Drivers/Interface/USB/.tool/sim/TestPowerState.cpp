#include <VHAL.h>
#include "DeviceFixture.h"
#include "Descriptors.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <array>
#include <span>

// Suspend, resume and remote wakeup. The host enables remote wakeup with SET_FEATURE(DEVICE_REMOTE_WAKEUP); the device may
// wake the host only while suspended and only when that was enabled. A bus reset turns the flag off.


namespace {


void ExpectIn(SimUSBDeviceAdapter& sim, const USBSetupBytes& setup, std::span<const uint8> expected) {
	std::array<uint8, 256> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(SimHost::Control(sim, setup, std::span<const uint8>(), in, received), ResultStatus::ok);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(received), expected));
}


void ConfigureTo(DeviceFixture& fixture) {
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0)), ResultStatus::ok);
}


void TestRemoteWakeupEnabled() {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	fixture.sim.InjectBusReset();
	ConfigureTo(fixture);

	// Not suspended: the wakeup is refused whether or not the host enabled it.
	SIM_EXPECT_EQ(fixture.device.RemoteWakeup(), ResultStatus::notReady);

	// The host enables remote wakeup. GET_STATUS shows it in bit 1, and bit 0 stays clear: the device is bus powered.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 3, 1, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT(fixture.device.IsRemoteWakeupEnabled());
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 0, 0x0000, 0x0000, 2), std::array<uint8, 2>{ 0x02, 0x00 });

	fixture.sim.InjectSuspend();
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Suspended);
	SIM_EXPECT_EQ(fixture.function.suspendCount, 1);

	// The wakeup starts the resume signalling and returns. The core is still Suspended until the port ends the signalling and
	// raises onResume, which FinishResume() injects.
	SIM_EXPECT_EQ(fixture.device.RemoteWakeup(), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Suspended);
	SIM_EXPECT_EQ(fixture.function.resumeCount, 0);

	SIM_EXPECT_EQ(fixture.sim.FinishResume(), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Configured);
	SIM_EXPECT_EQ(fixture.function.resumeCount, 1);
	SIM_EXPECT(fixture.device.IsRemoteWakeupEnabled());

	// Once resumed the device is not suspended, so another wakeup is refused.
	SIM_EXPECT_EQ(fixture.device.RemoteWakeup(), ResultStatus::notReady);
	SIM_EXPECT_EQ(fixture.sim.GetAddress(), 5);
	SIM_EXPECT_EQ(fixture.device.GetConfigurationValue(), 1);
}


void TestRemoteWakeupNotEnabled() {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	fixture.sim.InjectBusReset();
	ConfigureTo(fixture);

	// The host never enabled the wakeup, so a suspended device may not use it.
	fixture.sim.InjectSuspend();
	SIM_EXPECT_EQ(fixture.device.RemoteWakeup(), ResultStatus::notSupported);
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Suspended);
	SIM_EXPECT_EQ(fixture.function.resumeCount, 0);

	// The host resumes the bus instead. The state before the suspend comes back.
	fixture.sim.InjectResume();
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Configured);
	SIM_EXPECT_EQ(fixture.function.resumeCount, 1);
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 0, 0x0000, 0x0000, 2), std::array<uint8, 2>{ 0x00, 0x00 });
}


void TestEnableThenDisable() {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	fixture.sim.InjectBusReset();
	ConfigureTo(fixture);

	// SET_FEATURE enables, CLEAR_FEATURE disables. A disabled flag refuses the wakeup again.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 3, 1, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 1, 1, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT(!fixture.device.IsRemoteWakeupEnabled());
	fixture.sim.InjectSuspend();
	SIM_EXPECT_EQ(fixture.device.RemoteWakeup(), ResultStatus::notSupported);
	fixture.sim.InjectResume();

	// Only DEVICE_REMOTE_WAKEUP is a device feature here: another wValue stalls.
	std::array<uint8, 4> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 3, 2, 0x0000, 0), std::span<const uint8>(), in, received), ResultStatus::accessError);
}


// A bus reset turns the remote wakeup flag off. The host has to enable it again after the reset, and the device answers the
// status with the flag clear until then.
void TestBusResetClearsRemoteWakeup() {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	fixture.sim.InjectBusReset();
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 3, 1, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT(fixture.device.IsRemoteWakeupEnabled());

	fixture.sim.InjectBusReset();
	SIM_EXPECT(!fixture.device.IsRemoteWakeupEnabled());
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 0, 0x0000, 0x0000, 2), std::array<uint8, 2>{ 0x00, 0x00 });

	// After the address and the configuration, the flag is still clear, so a suspended device may not wake the host.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT(!fixture.device.IsRemoteWakeupEnabled());
	fixture.sim.InjectSuspend();
	SIM_EXPECT_EQ(fixture.device.RemoteWakeup(), ResultStatus::notSupported);
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Suspended);
	SIM_EXPECT_EQ(fixture.function.resumeCount, 0);
}


void TestSuspendFromAddressAndDefault() {
	// The device can be suspended in any state with an address or the default one, and it returns to that state.
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	fixture.sim.InjectBusReset();

	fixture.sim.InjectSuspend();
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Suspended);
	fixture.sim.InjectResume();
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Default);

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0)), ResultStatus::ok);
	fixture.sim.InjectSuspend();
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Suspended);
	fixture.sim.InjectResume();
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Address);
	SIM_EXPECT_EQ(fixture.device.GetAddress(), 5);
}


}


int main() {
	TestRemoteWakeupEnabled();
	TestRemoteWakeupNotEnabled();
	TestEnableThenDisable();
	TestBusResetClearsRemoteWakeup();
	TestSuspendFromAddressAndDefault();
	return SimTest::Finish("TestPowerState");
}
