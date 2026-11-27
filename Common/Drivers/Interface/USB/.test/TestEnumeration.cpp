#include <VHAL.h>
#include "DeviceFixture.h"
#include "Descriptors.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <array>
#include <span>

// Enumeration as the three host families run it. The orders follow what each host is documented to do; the bytes are the
// hand-written arrays of Descriptors.h.


namespace {


// Runs an IN request and checks the status and the bytes the host received. The buffer covers every answer in these tests.
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


void ExpectSetConfiguration(DeviceFixture& fixture, uint8 value) {
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, value, 0x0000, 0)), ResultStatus::ok);
}


void ExpectStringsAndBos(SimUSBDeviceAdapter& sim) {
	// String 0 is the LANGID list. It is asked with wIndex 0, and the others with 0x0409.
	ExpectIn(sim, SimHost::MakeSetup(0x80, 6, 0x0300, 0x0000, 255), SimDescriptors::StringLanguage);
	ExpectIn(sim, SimHost::MakeSetup(0x80, 6, 0x0301, 0x0409, 255), SimDescriptors::StringManufacturer);
	ExpectIn(sim, SimHost::MakeSetup(0x80, 6, 0x0302, 0x0409, 255), SimDescriptors::StringProduct);
	ExpectIn(sim, SimHost::MakeSetup(0x80, 6, 0x0303, 0x0409, 255), SimDescriptors::StringSerial);

	// The index of a string is what selects it: the language ID is ignored, so another LANGID gets the same bytes.
	ExpectIn(sim, SimHost::MakeSetup(0x80, 6, 0x0301, 0x0407, 255), SimDescriptors::StringManufacturer);

	// An index past the end of the string table is a stall.
	ExpectStall(sim, SimHost::MakeSetup(0x80, 6, 0x0304, 0x0409, 255));

	// BOS: the host reads the 5-byte header first, then the whole thing.
	ExpectIn(sim, SimHost::MakeSetup(0x80, 6, 0x0F00, 0x0000, 5), std::span<const uint8>(SimDescriptors::BosDescriptor).first(5));
	ExpectIn(sim, SimHost::MakeSetup(0x80, 6, 0x0F00, 0x0000, 255), SimDescriptors::BosDescriptor);
}


void TestWindowsOrder() {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Detached);
	fixture.sim.InjectBusReset();
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Default);

	// The first read asks for 64 bytes of the device descriptor at address 0 and gets its 18 bytes.
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 6, 0x0100, 0x0000, 64), SimDescriptors::DeviceDescriptor);
	SIM_EXPECT_EQ(fixture.sim.GetAddress(), 0);

	// Windows resets the bus once more, then sets the address.
	fixture.sim.InjectBusReset();
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.sim.GetAddress(), 5);
	SIM_EXPECT_EQ(fixture.device.GetAddress(), 5);
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Address);

	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 6, 0x0100, 0x0000, 18), SimDescriptors::DeviceDescriptor);

	// The configuration is read in two steps: the 9-byte header, then the full wTotalLength.
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 6, 0x0200, 0x0000, 9), std::span<const uint8>(SimDescriptors::ConfigurationDescriptor).first(9));
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 6, 0x0200, 0x0000, 255), SimDescriptors::ConfigurationDescriptor);

	ExpectStringsAndBos(fixture.sim);

	// The Microsoft OS 2.0 descriptor comes from vendor request 0x42 with wIndex 7.
	ExpectIn(fixture.sim, SimHost::MakeSetup(0xC0, 0x42, 0x0000, 0x0007, 0x00B2), SimDescriptors::MsOs20Set);

	// Asking with another wIndex is not this descriptor, so the device stalls.
	ExpectStall(fixture.sim, SimHost::MakeSetup(0xC0, 0x42, 0x0000, 0x0004, 0x00B2));

	ExpectSetConfiguration(fixture, 1);
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Configured);
	SIM_EXPECT_EQ(fixture.device.GetConfigurationValue(), 1);
	SIM_EXPECT_EQ(fixture.function.configureCount, 1);
	SIM_EXPECT(fixture.sim.FindEndpoint(0x81) != nullptr);
	SIM_EXPECT(fixture.sim.FindEndpoint(0x01) != nullptr);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
	SIM_EXPECT_EQ(fixture.sim.GetToggleMismatchCount(), 0);
}


void TestLinuxOrder() {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	fixture.sim.InjectBusReset();

	// Linux reads 64 bytes of the device descriptor, then sets the address once. It does not reset the bus again.
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 6, 0x0100, 0x0000, 64), SimDescriptors::DeviceDescriptor);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 5, 9, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.device.GetAddress(), 9);

	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 6, 0x0100, 0x0000, 18), SimDescriptors::DeviceDescriptor);
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 6, 0x0200, 0x0000, 9), std::span<const uint8>(SimDescriptors::ConfigurationDescriptor).first(9));
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 6, 0x0200, 0x0000, 32), SimDescriptors::ConfigurationDescriptor);

	ExpectStringsAndBos(fixture.sim);
	ExpectSetConfiguration(fixture, 1);
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Configured);
	SIM_EXPECT_EQ(fixture.sim.GetToggleMismatchCount(), 0);
}


void TestMacOrder() {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	fixture.sim.InjectBusReset();

	// macOS starts with the first 8 bytes of the device descriptor, which the host asks for with wLength 8. The answer is
	// cut to those bytes and no zero-length packet follows.
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 6, 0x0100, 0x0000, 8), std::span<const uint8>(SimDescriptors::DeviceDescriptor).first(8));

	fixture.sim.InjectBusReset();
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 5, 2, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.device.GetAddress(), 2);

	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 6, 0x0100, 0x0000, 18), SimDescriptors::DeviceDescriptor);
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 6, 0x0200, 0x0000, 9), std::span<const uint8>(SimDescriptors::ConfigurationDescriptor).first(9));
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 6, 0x0200, 0x0000, 32), SimDescriptors::ConfigurationDescriptor);
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 6, 0x0300, 0x0000, 255), SimDescriptors::StringLanguage);
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 6, 0x0302, 0x0409, 255), SimDescriptors::StringProduct);
	ExpectSetConfiguration(fixture, 1);
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Configured);
	SIM_EXPECT_EQ(fixture.sim.GetToggleMismatchCount(), 0);
}


void TestDeviceQualifierStalls() {
	// Full speed only: the device qualifier and the other-speed configuration describe a second speed, so they stall.
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	fixture.sim.InjectBusReset();
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x80, 6, 0x0600, 0x0000, 10));
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x80, 6, 0x0700, 0x0000, 9));
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 6, 0x0100, 0x0000, 18), SimDescriptors::DeviceDescriptor);
}


}


int main() {
	TestWindowsOrder();
	TestLinuxOrder();
	TestMacOrder();
	TestDeviceQualifierStalls();
	return SimTest::Finish("TestEnumeration");
}
