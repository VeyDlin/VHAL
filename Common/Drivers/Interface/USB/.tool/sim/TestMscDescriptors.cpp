#include <VHAL.h>
#include "MscHost.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <Drivers/Interface/USB/Class/MSC/USBMscBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include <array>
#include <cstddef>
#include <span>


// The mass storage descriptors against bytes written here by hand from USB 2.0 chapter 9 and the Mass Storage Class: the interface
// class 0x08, subclass 0x06 and protocol 0x50, the two bulk endpoints of 64 bytes, and the configuration that holds them. The builder
// does not produce the expected bytes, so a builder mistake cannot hide in both.


namespace {


// The device descriptor of the test identity (pid.codes 0x1209/0x0001): bcdUSB 2.00, class per interface, 64-byte control packets,
// manufacturer, product and serial strings 1, 2 and 3, one configuration.
constexpr std::array<uint8, 18> ExpectedDevice{
	0x12, 0x01,
	0x00, 0x02,
	0x00, 0x00, 0x00,
	0x40,
	0x09, 0x12,
	0x01, 0x00,
	0x00, 0x01,
	0x01, 0x02, 0x03,
	0x01
};


// The configuration: 32 bytes. The interface name "Mass storage" is the product's string, so it is string 2 (the strings are
// numbered in order of first use, and the same text gets the same number). Max power is 100 mA in 2 mA units.
constexpr std::array<uint8, 32> ExpectedConfiguration{
	0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
	0x09, 0x04, 0x00, 0x00, 0x02, 0x08, 0x06, 0x50, 0x02,
	0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,
	0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00
};


// The same function without a name: the interface string is 0.
using NamelessUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Mass storage")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBMscBuilder().BulkIn(0x81).BulkOut(0x02)));
}>;


// The endpoints left to the builder: the IN endpoint takes number 1 and the OUT endpoint number 2, both 64-byte bulk.
using AssignedUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Mass storage")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBMscBuilder().Name("Mass storage")));
}>;


// The interface's layout, as the configuration gives it to the function: interface 0, endpoints 0x81 and 0x02 in that order, and the
// interface and endpoint descriptors of the function's slice (9 + 7 + 7 bytes).
constexpr std::array<uint8, 1> ExpectedInterfaces{ 0x00 };
constexpr std::array<uint8, 2> ExpectedEndpoints{ 0x81, 0x02 };
constexpr std::array<uint8, 23> ExpectedSlice{
	0x09, 0x04, 0x00, 0x00, 0x02, 0x08, 0x06, 0x50, 0x02,
	0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,
	0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00
};


void TestDeviceAndConfigurationBytes() {
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(MscTest::MscUsb::Device), ExpectedDevice));
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(MscTest::MscUsb::Configuration), ExpectedConfiguration));
}


void TestFunctionLayout() {
	USBFunctionLayout layout = MscTest::MscUsb::Function(0);
	SIM_EXPECT(SimHost::SameBytes(layout.interfaces, ExpectedInterfaces));
	SIM_EXPECT(SimHost::SameBytes(layout.endpoints, ExpectedEndpoints));
	SIM_EXPECT(SimHost::SameBytes(layout.descriptors, ExpectedSlice));
}


void TestNameAndAssignedAddresses() {
	// Without a name the interface string is 0, and the endpoints are the ones given.
	constexpr std::array<uint8, 9> nameless{ 0x09, 0x04, 0x00, 0x00, 0x02, 0x08, 0x06, 0x50, 0x00 };
	std::span<const uint8> configuration = NamelessUsb::Configuration;
	SIM_EXPECT(SimHost::SameBytes(configuration.subspan(9, 9), nameless));

	// With no address given, the builder numbers the endpoints from 1 in the order IN, OUT: 0x81 and 0x02.
	std::span<const uint8> assigned = AssignedUsb::Configuration;
	SIM_EXPECT(SimHost::SameBytes(assigned.subspan(18, 7), std::span<const uint8>(ExpectedConfiguration).subspan(18, 7)));
	SIM_EXPECT(SimHost::SameBytes(assigned.subspan(25, 7), std::span<const uint8>(ExpectedConfiguration).subspan(25, 7)));
	SIM_EXPECT_EQ(AssignedUsb::EndpointAddresses[0], 0x81);
	SIM_EXPECT_EQ(AssignedUsb::EndpointAddresses[1], 0x02);
}


}


int main() {
	TestDeviceAndConfigurationBytes();
	TestFunctionLayout();
	TestNameAndAssignedAddresses();
	return SimTest::Finish("TestMscDescriptors");
}
