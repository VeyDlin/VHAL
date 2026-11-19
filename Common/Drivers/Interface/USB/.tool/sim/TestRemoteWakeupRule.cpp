#include <VHAL.h>
#include "Descriptors.h"
#include "SimAssert.h"
#include "SimHost.h"
#include "SimUSBDeviceAdapter.h"
#include <Drivers/Interface/USB/Class/Vendor/VendorBulkFunction.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <array>
#include <span>

// SET_FEATURE(DEVICE_REMOTE_WAKEUP) is a request error when the configuration descriptor does not advertise remote wakeup
// (bmAttributes bit 5). The two configurations below differ only in that bit, so the same request is checked against both.
// CLEAR_FEATURE needs no such check and is accepted either way.


namespace {


constexpr uint8 Interfaces[] = {
	0
};


constexpr uint8 Endpoints[] = {
	0x81,
	0x01
};


// Bus powered, remote wakeup advertised (bmAttributes 0xA0). One vendor interface, bulk 0x81 and 0x01, as in SimpleSet.
constexpr uint8 AdvertisingConfiguration[] = {
	0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0xA0, 0x32,
	0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00,
	0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,
	0x07, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00
};


// The same configuration with bmAttributes 0x80: remote wakeup is not advertised.
constexpr uint8 SilentConfiguration[] = {
	0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
	0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00,
	0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,
	0x07, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00
};


using VendorFunction = VendorBulkFunction<100, 256, 2>;


// The shared descriptors with the given configuration. The device keeps a copy of the set, and the arrays have static storage.
USBDescriptorSet MakeSet(std::span<const uint8> configuration) {
	USBDescriptorSet set = SimDescriptors::SimpleSet;
	set.configuration = configuration;
	return set;
}


struct Fixture {
	SimUSBDeviceAdapter sim;
	VendorFunction function;
	std::array<USBFunction*, 1> functions;
	USBDevice device;


	explicit Fixture(std::span<const uint8> configuration)
		: function(Interfaces, Endpoints),
		  functions{ &function },
		  device(sim, MakeSet(configuration), functions) {
	}


	// Attaches the device and configures it, as a host does.
	void Configure() {
		SIM_EXPECT_EQ(device.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);
		sim.InjectBusReset();
		SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0)), ResultStatus::ok);
		SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0)), ResultStatus::ok);
		SIM_EXPECT_EQ(device.GetState(), USBDeviceState::Configured);
	}
};


// The host enables remote wakeup on a configuration that advertises it.
void CheckAdvertisedConfigurationAccepts() {
	Fixture fixture(AdvertisingConfiguration);
	fixture.Configure();

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 3, 1, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT(fixture.device.IsRemoteWakeupEnabled());
}


// The same request on a configuration that does not advertise it stalls, and the flag stays clear. The stall does not leave
// endpoint 0 stuck: the next request is answered.
void CheckSilentConfigurationStalls() {
	Fixture fixture(SilentConfiguration);
	fixture.Configure();

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 3, 1, 0x0000, 0)), ResultStatus::accessError);
	SIM_EXPECT(!fixture.device.IsRemoteWakeupEnabled());

	std::array<uint8, 2> status = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x80, 0, 0x0000, 0x0000, 2), std::span<const uint8>(), status, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 2);
	SIM_EXPECT_EQ(status[0], 0x00);

	// CLEAR_FEATURE is accepted on the silent configuration, so a host that clears what it never set gets no error.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 1, 1, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT(!fixture.device.IsRemoteWakeupEnabled());
}


// The other device feature numbers are still refused, whichever configuration is used.
void CheckOtherFeaturesStill() {
	Fixture advertising(AdvertisingConfiguration);
	advertising.Configure();
	SIM_EXPECT_EQ(SimHost::Control(advertising.sim, SimHost::MakeSetup(0x00, 3, 2, 0x0000, 0)), ResultStatus::accessError);
}


}


int main() {
	CheckAdvertisedConfigurationAccepts();
	CheckSilentConfigurationStalls();
	CheckOtherFeaturesStill();
	return SimTest::Finish("TestRemoteWakeupRule");
}
