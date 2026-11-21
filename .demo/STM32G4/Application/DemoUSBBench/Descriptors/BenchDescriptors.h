#pragma once
#include <BenchProtocol.h>
#include <DemoProducts.h>
#include <Drivers/Interface/USB/Class/Vendor/USBVendorBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBConfigurationBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include <Drivers/Interface/USB/Descriptor/MsOs20/USBMsOs20DescriptorSet.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorComposition.h>


// The descriptors of the benchmark device, written with the fluent builder. The chain runs at compile time and every array lives in
// flash, as USBDevice requires. The identities come from BenchProtocol.h; the library itself has none.
//
// The order of the calls sets the string indexes: manufacturer 1, product 2, the serial number 3 and the interface name 4. The serial
// number string is filled in at run time by USBDevice::SetSerialNumber() from the MCU's unique ID (DemoUSBBench.cpp).
using BenchUsb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(BenchProtocol::VendorId)
		.ProductId(BenchProtocol::ProductId)
		.DeviceRelease(BenchProtocol::DeviceRelease)
		.Manufacturer("VHAL")
		.Product("USB Bench")
		.SerialNumberAtRuntime()
		.MsOs20VendorCode(BenchProtocol::MsOs20VendorCode)
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBVendorBuilder()
				.Name("Vendor bulk")
				.BulkIn(BenchProtocol::BulkInAddress)
				.BulkOut(BenchProtocol::BulkOutAddress)
				.WinUsb(BenchProtocol::InterfaceGuid)));
}>;


// usbbench finds the device by these values, and the host's requests are answered from these bytes. The asserts read the bytes the
// core will serve, so a value written at the wrong offset fails the build instead of the PC benchmark.
// Offsets: device descriptor idVendor 8, idProduct 10, bcdDevice 12. Configuration: interface number 9 + 2, bulk IN address 18 + 2 and
// wMaxPacketSize 18 + 4, bulk OUT address 25 + 2 and wMaxPacketSize 25 + 4.
static_assert(USBDescriptorRead16(BenchUsb::Device, 8) == BenchProtocol::VendorId, "idVendor must be BenchProtocol::VendorId");
static_assert(USBDescriptorRead16(BenchUsb::Device, 10) == BenchProtocol::ProductId, "idProduct must be BenchProtocol::ProductId");
// The product table of the demos (DemoProducts.h) lists the bench's PID too, for the PC tools. This keeps it equal to the bench's own value.
static_assert(BenchProtocol::ProductId == DemoProducts::BenchProductId, "DemoProducts::BenchProductId must be BenchProtocol::ProductId");
static_assert(USBDescriptorRead16(BenchUsb::Device, 12) == BenchProtocol::DeviceRelease, "bcdDevice must be BenchProtocol::DeviceRelease");
static_assert(BenchUsb::Configuration[11] == BenchProtocol::InterfaceNumber, "the vendor interface must be BenchProtocol::InterfaceNumber");
static_assert(BenchUsb::Configuration[20] == BenchProtocol::BulkInAddress, "the bulk IN endpoint must be BenchProtocol::BulkInAddress");
static_assert(BenchUsb::Configuration[27] == BenchProtocol::BulkOutAddress, "the bulk OUT endpoint must be BenchProtocol::BulkOutAddress");
static_assert(USBDescriptorRead16(BenchUsb::Configuration, 22) == BenchProtocol::BulkPacketSize, "bulk IN packet size must be BenchProtocol::BulkPacketSize");
static_assert(USBDescriptorRead16(BenchUsb::Configuration, 29) == BenchProtocol::BulkPacketSize, "bulk OUT packet size must be BenchProtocol::BulkPacketSize");
// The MS OS 2.0 vendor code is in the BOS: 5 bytes of BOS header, then 20 bytes of platform capability header and UUID, then 8
// bytes of its data (dwWindowsVersion, wMSOSDescriptorSetTotalLength, bMS_VendorCode, bAltEnumCode). bMS_VendorCode is at 5 + 20 + 6 = 31.
static_assert(BenchUsb::Bos[31] == BenchProtocol::MsOs20VendorCode, "the BOS must carry BenchProtocol::MsOs20VendorCode");
static_assert(BenchUsb::VendorRequests.size() == 1, "one vendor request, the MS OS 2.0 descriptor");
static_assert(BenchUsb::VendorRequests[0].request == BenchProtocol::MsOs20VendorCode, "the vendor request must be BenchProtocol::MsOs20VendorCode");
static_assert(BenchUsb::VendorRequests[0].index == USBMsOs20DescriptorIndex, "the MS OS 2.0 descriptor is requested with wIndex 7");
static_assert(USBDeviceClassMatchesConfiguration(BenchUsb::Device, BenchUsb::Configuration), "device class must match the configuration");
static_assert(USBDeviceSupportsBos(BenchUsb::Device), "a BOS needs bcdUSB 2.01 or later");
