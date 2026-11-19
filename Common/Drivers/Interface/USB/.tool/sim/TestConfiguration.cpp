#include <VHAL.h>
#include "DeviceFixture.h"
#include "Descriptors.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <array>
#include <span>

// Configuration: SET_CONFIGURATION with 0, a repeated and an unknown value, a configuration that fails half-way, the
// GET_CONFIGURATION, GET_INTERFACE and SET_INTERFACE requests, and a bus reset while configured.


namespace {


constexpr uint8 FirstInterfaces[] = {
	0
};


constexpr uint8 FirstEndpoints[] = {
	0x81,
	0x01
};


constexpr uint8 SecondInterfaces[] = {
	1
};


constexpr uint8 SecondEndpoints[] = {
	0x82,
	0x02
};


// The simulator's device descriptor with another bcdUSB.
constexpr std::array<uint8, 18> WithUsbVersion(uint16 usbVersion) {
	std::array<uint8, 18> bytes{};
	for (std::size_t i = 0; i < bytes.size(); i++) {
		bytes[i] = SimDescriptors::DeviceDescriptor[i];
	}
	bytes[2] = static_cast<uint8>(usbVersion);
	bytes[3] = static_cast<uint8>(usbVersion >> 8);
	return bytes;
}


constexpr std::array<uint8, 18> UsbTwoZeroDevice = WithUsbVersion(0x0200);


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


void ConfigureTo(DeviceFixture& fixture) {
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0)), ResultStatus::ok);
}


void TestSetConfigurationValues() {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	fixture.sim.InjectBusReset();
	ConfigureTo(fixture);
	SIM_EXPECT_EQ(fixture.device.GetConfigurationValue(), 1);
	SIM_EXPECT_EQ(fixture.function.configureCount, 1);

	// A repeat of the same value deconfigures first, then configures again.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.function.deconfigureCount, 1);
	SIM_EXPECT_EQ(fixture.function.configureCount, 2);
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Configured);

	// SET_CONFIGURATION 0 deconfigures: the function drops its work, the endpoints close, endpoint 0 stays open.
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x01));
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 0, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.function.deconfigureCount, 2);
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Address);
	SIM_EXPECT_EQ(fixture.device.GetConfigurationValue(), 0);
	SIM_EXPECT(fixture.sim.FindEndpoint(0x81) == nullptr);
	SIM_EXPECT(fixture.sim.FindEndpoint(0x01) == nullptr);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x01));
	SIM_EXPECT(fixture.sim.FindEndpoint(0x80) != nullptr);

	// Zero again when not configured: nothing is deconfigured twice.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 0, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.function.deconfigureCount, 2);

	// Only the value of the descriptor is accepted, so 2 stalls.
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x00, 9, 2, 0x0000, 0));
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Address);
}


void TestBusResetWhileConfigured() {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	fixture.sim.InjectBusReset();
	ConfigureTo(fixture);

	// A bus reset resets the function and closes its endpoints, but it does not call OnDeconfigured.
	fixture.sim.InjectBusReset();
	SIM_EXPECT_EQ(fixture.function.busResetCount, 2);
	SIM_EXPECT_EQ(fixture.function.deconfigureCount, 0);
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Default);
	SIM_EXPECT_EQ(fixture.device.GetConfigurationValue(), 0);
	SIM_EXPECT(fixture.sim.FindEndpoint(0x81) == nullptr);
}


void TestGetConfigurationAndStates() {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	fixture.sim.InjectBusReset();

	// GET_CONFIGURATION is answered in the Address and Configured states only.
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x80, 8, 0x0000, 0x0000, 1));
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0)), ResultStatus::ok);
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 8, 0x0000, 0x0000, 1), std::array<uint8, 1>{ 0x00 });
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0)), ResultStatus::ok);
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 8, 0x0000, 0x0000, 1), std::array<uint8, 1>{ 0x01 });

	// GET_INTERFACE: the owned interface answers its alternate setting, an unowned one stalls.
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x81, 10, 0x0000, 0x0000, 1), std::array<uint8, 1>{ 0x00 });
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x81, 10, 0x0000, 0x0005, 1));

	// SET_INTERFACE: alternate 0 is the only one this function has, so 1 stalls and 0 is accepted.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x01, 11, 0x0000, 0x0000, 0)), ResultStatus::ok);
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x01, 11, 0x0001, 0x0000, 0));
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x01, 11, 0x0000, 0x0005, 0));

	// GET_STATUS of an owned interface answers two zero bytes.
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x81, 0, 0x0000, 0x0000, 2), std::array<uint8, 2>{ 0x00, 0x00 });
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x81, 0, 0x0000, 0x0005, 2));

	// Back in Address, the interface requests need Configured.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 0, 0x0000, 0)), ResultStatus::ok);
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x81, 10, 0x0000, 0x0000, 1));
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x01, 11, 0x0000, 0x0000, 0));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


void TestFailingOnConfiguredRollsBack() {
	SimUSBDeviceAdapter sim;
	TestFunction first(FirstInterfaces, FirstEndpoints, 0x81, 0x01);
	TestFunction second(SecondInterfaces, SecondEndpoints, 0x82, 0x02);
	second.failConfigure = true;
	std::array<USBFunction*, 2> functions = { &first, &second };
	USBDevice device(sim, SimDescriptors::CompositeSet, functions);

	SIM_EXPECT_EQ(device.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);
	sim.InjectBusReset();
	SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0)), ResultStatus::ok);

	// The second function fails. The first one is already configured, so it is deconfigured again, the endpoints close and
	// the request stalls. The device is left in Address with configuration 0.
	ExpectStall(sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0));
	SIM_EXPECT_EQ(first.configureCount, 1);
	SIM_EXPECT_EQ(first.deconfigureCount, 1);
	SIM_EXPECT_EQ(second.deconfigureCount, 0);
	SIM_EXPECT_EQ(device.GetState(), USBDeviceState::Address);
	SIM_EXPECT_EQ(device.GetConfigurationValue(), 0);
	SIM_EXPECT(sim.FindEndpoint(0x81) == nullptr);
	SIM_EXPECT(sim.FindEndpoint(0x82) == nullptr);

	// Once the second function works, the same request configures both.
	second.failConfigure = false;
	SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(device.GetState(), USBDeviceState::Configured);
	SIM_EXPECT_EQ(first.configureCount, 2);
	SIM_EXPECT_EQ(second.configureCount, 2);
	SIM_EXPECT(sim.FindEndpoint(0x82) != nullptr);
	SIM_EXPECT(sim.FindEndpoint(0x02) != nullptr);
}


// Hosts ask for the BOS only from bcdUSB 2.01 on, so a set with a BOS behind bcdUSB 2.00 is refused at Start(). The same
// device descriptor without a BOS starts.
void TestBosNeedsUsbTwoZeroOne() {
	const USBDescriptorSet withBos = {
		UsbTwoZeroDevice,
		SimDescriptors::ConfigurationDescriptor,
		SimDescriptors::StringTable,
		SimDescriptors::BosDescriptor,
		SimDescriptors::VendorRequests
	};
	const USBDescriptorSet withoutBos = {
		UsbTwoZeroDevice,
		SimDescriptors::ConfigurationDescriptor,
		SimDescriptors::StringTable,
		std::span<const uint8>(),
		std::span<const USBVendorDescriptor>()
	};

	SimUSBDeviceAdapter refusedSim;
	TestFunction refusedFunction(FirstInterfaces, FirstEndpoints, 0x81, 0x01);
	std::array<USBFunction*, 1> refusedFunctions = { &refusedFunction };
	USBDevice refused(refusedSim, withBos, refusedFunctions);
	SIM_EXPECT_EQ(refused.Start(USBDeviceAdapter::Parameters()), ResultStatus::invalidParameter);

	SimUSBDeviceAdapter startedSim;
	TestFunction startedFunction(FirstInterfaces, FirstEndpoints, 0x81, 0x01);
	std::array<USBFunction*, 1> startedFunctions = { &startedFunction };
	USBDevice started(startedSim, withoutBos, startedFunctions);
	SIM_EXPECT_EQ(started.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);
}


}


int main() {
	TestSetConfigurationValues();
	TestBusResetWhileConfigured();
	TestGetConfigurationAndStates();
	TestFailingOnConfiguredRollsBack();
	TestBosNeedsUsbTwoZeroOne();
	return SimTest::Finish("TestConfiguration");
}
