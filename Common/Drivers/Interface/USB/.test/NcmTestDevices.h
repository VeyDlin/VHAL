#pragma once
#include <VHAL.h>
#include <Drivers/Interface/USB/Class/CDC/NcmFunction.h>
#include <Drivers/Interface/USB/Class/CDC/USBCdcNcmBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include <Drivers/Interface/USB/Descriptor/USBHexStringDescriptor.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include "SimAssert.h"
#include "SimHost.h"
#include "SimTestHelpers.h"
#include "SimUSBDeviceAdapter.h"
#include <array>
#include <cstddef>
#include <cstdio>
#include <span>


// The CDC-NCM test devices: the descriptor sets with automatic and explicit endpoint addresses, the MAC run-time string, and a fixture
// that attaches one device the way a host does. The expected bytes are not here; each test writes its own from the specification.
namespace NcmTest {


// Strings: manufacturer 1, product 2, serial 3, the communication interface's name 4, then the run-time MAC string 5.
// Automatic addresses: notification 0x81, bulk IN 0x82, bulk OUT 0x03 (each number unused in either direction when it is taken).
using AutoUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0002)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("NCM")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBCdcNcmBuilder().Name("Ethernet")));
}>;


// Explicit addresses on three numbers: notification 0x83, bulk IN 0x81, bulk OUT 0x02. The fixtures use these.
using ExplicitUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0002)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("NCM")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBCdcNcmBuilder().Name("Ethernet").NotificationIn(0x83).BulkIn(0x81).BulkOut(0x02)));
}>;


// The same explicit layout with a 4 ms notification interval.
using IntervalUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0002)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("NCM")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBCdcNcmBuilder().Name("Ethernet").NotificationIn(0x83).IntervalMs(4).BulkIn(0x81).BulkOut(0x02)));
}>;


// The function as the tests use it: 2048-byte NTBs in both directions and four frames in each queue.
using Function = NcmFunction<2048, 2048, 4, 4>;


// The same function with a 4096-byte input size, for the tests where the host lowers SET_NTB_INPUT_SIZE below the largest NTB.
using WideFunction = NcmFunction<4096, 2048, 4, 4>;


// The host's MAC address for the run-time string: 02 00 00 AB CD EF, as the 26-byte string descriptor. It is static, because the device
// keeps the span of the descriptor until it is stopped.
inline constexpr std::array<uint8, 6> MacBytes = {0x02, 0x00, 0x00, 0xAB, 0xCD, 0xEF};


inline constexpr std::array<uint8, 26> MacDescriptor = []() {
	std::array<uint8, 26> descriptor{};
	MakeUSBHexStringDescriptor(MacBytes, descriptor);
	return descriptor;
}();


// Attaches one device with an NCM function built from the descriptor set TUsb, the way an application does. The MAC string is given
// before the device starts, as the device requires, and the device is configured when Configure() is called.
// TAdapter is SimUSBDeviceAdapter or a class a test derives from it, to refuse or hold back a call of the function.
template<typename TUsb, typename TFunction = Function, typename TAdapter = SimUSBDeviceAdapter>
struct NcmFixtureOf {
	TAdapter sim;
	TFunction function;
	std::array<USBFunction*, 1> functions;
	USBDevice device;


	NcmFixtureOf()
		: function(TUsb::Function(0)),
		  functions{ &function },
		  device(sim, TUsb::Set, functions) {
	}


	// Gives the MAC string, starts the device and enumerates it, as a host does.
	void Configure() {
		SIM_EXPECT_EQ(device.SetRuntimeString(function.MacStringIndex(), MacDescriptor), ResultStatus::ok);
		SIM_EXPECT_EQ(device.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);
		Enumerate();
	}


	// Resets the bus, sets the address and selects configuration 1, as a host does after a reset.
	void Enumerate() {
		SimTest::EnumerateDevice(sim, device);
	}


	// SET_CONFIGURATION(0): the device leaves the configuration.
	ResultStatus Deconfigure() {
		return SimHost::Control(sim, SimHost::MakeSetup(0x00, 9, 0, 0x0000, 0));
	}


	// SET_INTERFACE of the data interface (interface 1) to the alternate setting.
	ResultStatus SelectAlternate(uint16 alternate) {
		return SimHost::Control(sim, SimHost::MakeSetup(0x01, 11, alternate, 0x0001, 0));
	}
};


using NcmFixture = NcmFixtureOf<ExplicitUsb>;


// The fixture of WideFunction, on the same explicit addresses.
using WideNcmFixture = NcmFixtureOf<ExplicitUsb, WideFunction>;


}


// The fixtures are used unqualified by the NCM tests; the names are kept in NcmTest for the descriptor sets beside them.
using NcmTest::NcmFixture;
using NcmTest::WideNcmFixture;


