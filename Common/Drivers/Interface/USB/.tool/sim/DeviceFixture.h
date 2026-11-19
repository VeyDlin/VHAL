#pragma once
#include <VHAL.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include "Descriptors.h"
#include "SimUSBDeviceAdapter.h"
#include "TestFunction.h"
#include <array>


namespace SimFixtureData {


inline constexpr uint8 Interfaces[] = {
	0
};


inline constexpr uint8 Endpoints[] = {
	0x81,
	0x01
};


}


// One device with a single TestFunction (bulk 0x81 and 0x01) on the simple descriptor set. Start() attaches it the way an
// application does. A test injects its own bus reset, so each test chooses its enumeration order.
struct DeviceFixture {
	SimUSBDeviceAdapter sim;
	TestFunction function;
	std::array<USBFunction*, 1> functions;
	USBDevice device;


	DeviceFixture()
		: function(SimFixtureData::Interfaces, SimFixtureData::Endpoints, 0x81, 0x01),
		  functions{ &function },
		  device(sim, SimDescriptors::SimpleSet, functions) {
	}


	ResultStatus Start() {
		return device.Start(USBDeviceAdapter::Parameters());
	}
};
