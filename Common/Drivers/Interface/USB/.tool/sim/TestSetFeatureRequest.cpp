#include <VHAL.h>
#include "Descriptors.h"
#include "SimAssert.h"
#include "SimHost.h"
#include "SimUSBDeviceAdapter.h"
#include <Drivers/Interface/USB/Class/Vendor/VendorBulkFunction.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <array>
#include <span>

// SET_FEATURE and CLEAR_FEATURE have a fixed shape (USB 2.0 sections 9.4.1 and 9.4.9). The device form has wIndex 0 and no data
// stage. The endpoint form has no data stage, and wIndex is the endpoint itself. A request of the wrong shape is stalled, and
// neither the remote wakeup flag nor the endpoint halt changes. The good request that follows still works, so the stall does
// not leave the control pipe stuck.


namespace {


constexpr uint8 Interfaces[] = {
	0
};


constexpr uint8 Endpoints[] = {
	0x81,
	0x01
};


// Bus powered, remote wakeup advertised (bmAttributes 0xA0). One vendor interface, bulk 0x81 and 0x01.
constexpr uint8 Configuration[] = {
	0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0xA0, 0x32,
	0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00,
	0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,
	0x07, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00
};


using VendorFunction = VendorBulkFunction<100, 256, 2>;


USBDescriptorSet MakeSet() {
	USBDescriptorSet set = SimDescriptors::SimpleSet;
	set.configuration = Configuration;
	return set;
}


struct Fixture {
	SimUSBDeviceAdapter sim;
	VendorFunction function;
	std::array<USBFunction*, 1> functions;
	USBDevice device;


	Fixture()
		: function(Interfaces, Endpoints),
		  functions{ &function },
		  device(sim, MakeSet(), functions) {
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


// The device form with a non-zero wIndex is refused, for SET_FEATURE and for CLEAR_FEATURE. The flag keeps its value.
void CheckDeviceWIndexRefused() {
	Fixture fixture;
	fixture.Configure();

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 3, 1, 0x0001, 0)), ResultStatus::accessError);
	SIM_EXPECT(!fixture.device.IsRemoteWakeupEnabled());

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 3, 1, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT(fixture.device.IsRemoteWakeupEnabled());

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 1, 1, 0x0001, 0)), ResultStatus::accessError);
	SIM_EXPECT(fixture.device.IsRemoteWakeupEnabled());
}


// The device form with a data length is refused. The request has no data stage, so a status stage would not match the host.
void CheckDeviceWLengthRefused() {
	Fixture fixture;
	fixture.Configure();

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 3, 1, 0x0000, 2)), ResultStatus::accessError);
	SIM_EXPECT(!fixture.device.IsRemoteWakeupEnabled());

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 3, 1, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT(fixture.device.IsRemoteWakeupEnabled());
}


// The endpoint form with a data length is refused, and the halt does not start. The same request with no length halts the
// endpoint, and CLEAR_FEATURE with no length clears it.
void CheckEndpointWLengthRefused() {
	Fixture fixture;
	fixture.Configure();

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 3, 0, 0x0081, 2)), ResultStatus::accessError);
	SIM_EXPECT(!fixture.sim.IsStalled(0x81));

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 3, 0, 0x0081, 0)), ResultStatus::ok);
	SIM_EXPECT(fixture.sim.IsStalled(0x81));

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 1, 0, 0x0081, 2)), ResultStatus::accessError);
	SIM_EXPECT(fixture.sim.IsStalled(0x81));

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 1, 0, 0x0081, 0)), ResultStatus::ok);
	SIM_EXPECT(!fixture.sim.IsStalled(0x81));
}


}


int main() {
	CheckDeviceWIndexRefused();
	CheckDeviceWLengthRefused();
	CheckEndpointWLengthRefused();
	return SimTest::Finish("TestSetFeatureRequest");
}
