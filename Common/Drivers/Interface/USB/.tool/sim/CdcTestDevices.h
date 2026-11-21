#pragma once
#include <VHAL.h>
#include <Drivers/Interface/USB/Class/CDC/CdcAcmFunction.h>
#include <Drivers/Interface/USB/Class/CDC/USBCdcAcmBuilder.h>
#include <Drivers/Interface/USB/Class/Vendor/USBVendorBuilder.h>
#include <Drivers/Interface/USB/Class/Vendor/VendorBulkFunction.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include "SimAssert.h"
#include "SimHost.h"
#include "SimTestHelpers.h"
#include "SimUSBDeviceAdapter.h"
#include <array>
#include <cstddef>
#include <span>


// The CDC-ACM test devices: the descriptor sets with automatic and explicit endpoint addresses, and fixtures that attach one device the
// way a host does. The expected bytes are not here; each test writes its own from the specification.
namespace CdcTest {


// Strings: manufacturer 1, product 2, serial 3, then the communication interface's name 4 (numbered in order of first use).
// Automatic addresses: notification 0x81, bulk IN 0x82, bulk OUT 0x03 (each number unused in either direction when it is taken).
using AutoUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("CDC")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBCdcAcmBuilder().Name("Serial")));
}>;


// Explicit addresses on three separate numbers: notification 0x83, bulk IN 0x81, bulk OUT 0x02. The data of the fixtures below uses these.
using ExplicitUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("CDC")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBCdcAcmBuilder().Name("Serial").NotificationIn(0x83).BulkIn(0x81).BulkOut(0x02)));
}>;


// The same explicit layout with a 4 ms notification interval.
using IntervalUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("CDC")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBCdcAcmBuilder().Name("Serial").NotificationIn(0x83).IntervalMs(4).BulkIn(0x81).BulkOut(0x02)));
}>;


// A vendor bulk device on the same explicit bulk pair, for the bootloader transport over a vendor function.
using VendorUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Vendor")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBVendorBuilder().BulkIn(0x81).BulkOut(0x02)));
}>;


// The CDC function as the tests use it: a 512-byte TX ring, a 256-byte RX ring and 128-byte transfers.
using Function = CdcAcmFunction<512, 256, 2>;


// Attaches one device with a CDC-ACM function built from the descriptor set TUsb, the way an application does, and configures it when
// Configure() is called. The function is made from the builder's layout, so the test also checks that path.
template<typename TUsb>
struct CdcFixtureOf {
	SimUSBDeviceAdapter sim;
	Function function;
	std::array<USBFunction*, 1> functions;
	USBDevice device;


	CdcFixtureOf()
		: function(TUsb::Function(0)),
		  functions{ &function },
		  device(sim, TUsb::Set, functions) {
	}


	// Starts the device and enumerates it, as a host does.
	void Configure() {
		SIM_EXPECT_EQ(device.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);
		Enumerate();
	}


	// Resets the bus, sets the address and selects configuration 1, as a host does after a reset.
	void Enumerate() {
		SimTest::EnumerateDevice(sim, device);
	}


	// SET_CONTROL_LINE_STATE to the communication interface (0x21 is class, interface, host to device).
	ResultStatus SetControlLines(uint16 value) {
		return SimHost::Control(sim, SimHost::MakeSetup(0x21, 0x22, value, 0x0000, 0));
	}
};


using CdcFixture = CdcFixtureOf<ExplicitUsb>;


// A vendor bulk function on the explicit bulk pair, with the same ring sizes as the CDC function.
template<typename TUsb>
struct VendorFixtureOf {
	SimUSBDeviceAdapter sim;
	VendorBulkFunction<512, 256, 2> function;
	std::array<USBFunction*, 1> functions;
	USBDevice device;


	VendorFixtureOf()
		: function(TUsb::Function(0)),
		  functions{ &function },
		  device(sim, TUsb::Set, functions) {
	}


	void Configure() {
		SIM_EXPECT_EQ(device.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);
		SimTest::EnumerateDevice(sim, device);
	}
};


}


// The fixtures are used unqualified by the CDC tests; the names are kept in CdcTest for the descriptor sets beside them.
using CdcTest::CdcFixture;
using CdcTest::VendorFixtureOf;
