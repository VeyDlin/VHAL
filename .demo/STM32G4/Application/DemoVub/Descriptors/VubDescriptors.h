#pragma once
#include <DemoProducts.h>
#include <Drivers/Interface/USB/Class/CDC/USBCdcAcmBuilder.h>
#include <Drivers/Interface/USB/Class/HID/USBHidBuilder.h>
#include <Drivers/Interface/USB/Class/HID/USBHidReportBuilder.h>
#include <Drivers/Interface/USB/Class/Vendor/USBVendorBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBConfigurationBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include <Drivers/Interface/USB/Descriptor/MsOs20/USBMsOs20DescriptorSet.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorComposition.h>


// The report descriptor of the vendor HID interface: usage page 0xFF00, a 64-byte input report and a 64-byte output report, no report IDs.
using VubHidReport = USBHidReportDescriptor<+[] { return USBHidReportBuilder::VendorRaw(64); }>;


// The VUB demo device: a CDC-ACM serial port (interfaces 0 and 1), a vendor bulk interface with WinUSB (interface 2) and a vendor HID
// interface (interface 3). The functions take their interface numbers in that order, and the endpoints are numbered by the builder, one
// number per endpoint, so IN and OUT never share a number. Seven endpoints take numbers 1 to 7, which is the EndpointNumberLimit(7).
//
// The string indexes are: manufacturer 1, product 2, the serial number 3 (filled in at run time) and the function names 4 to 6.
using VubUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(DemoProducts::VendorId)
		.ProductId(DemoProducts::VubProductId)
		.DeviceRelease(DemoProducts::DeviceRelease)
		.Manufacturer("VHAL")
		.Product("VUB Demo")
		.SerialNumberAtRuntime()
		.MsOs20VendorCode(DemoProducts::VubMsOs20VendorCode)
		.EndpointNumberLimit(7)
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBCdcAcmBuilder()
				.Name("Serial"))
			.Function(USBVendorBuilder()
				.Name("Vendor bulk")
				.WinUsb(DemoProducts::VubInterfaceGuid))
			.Function(USBHidBuilder()
				.Name("Vendor HID")
				.Report(VubHidReport::Info)
				.OutEndpoint()));
}>;


// Offsets in the configuration: the header is 9 bytes, then the functions in order. CDC-ACM: interface association 9, communication
// interface 17 (number at 19), notification endpoint 45 (address at 47), data interface 52 (number at 54), bulk IN 61 (address at 63,
// size at 65), bulk OUT 68 (address at 70, size at 72). Vendor bulk: interface 75 (number at 77), bulk IN 84 (address at 86, size at 88),
// bulk OUT 91 (address at 93, size at 95). HID: interface 98 (number at 100), HID descriptor 107 (report length at 114), IN endpoint 116
// (address at 118, size at 120), OUT endpoint 123 (address at 125, size at 127). The configuration is 130 bytes.
static_assert(USBDescriptorRead16(VubUsb::Device, 8) == DemoProducts::VendorId, "idVendor must be DemoProducts::VendorId");
static_assert(USBDescriptorRead16(VubUsb::Device, 10) == DemoProducts::VubProductId, "idProduct must be DemoProducts::VubProductId");
static_assert(USBDescriptorRead16(VubUsb::Device, 12) == DemoProducts::DeviceRelease, "bcdDevice must be DemoProducts::DeviceRelease");
static_assert(USBDescriptorRead16(VubUsb::Configuration, 2) == 130, "the configuration is 130 bytes");
static_assert(VubUsb::Configuration[4] == 4, "the configuration has four interfaces");
static_assert(VubHidReport::Info.descriptor.size() > 0, "the HID report descriptor is built");
static_assert(USBDescriptorRead16(VubUsb::Configuration, 114) == VubHidReport::Info.descriptor.size(), "the HID descriptor gives the report length");

static_assert(VubUsb::Configuration[19] == 0, "the CDC communication interface is 0");
static_assert(VubUsb::Configuration[47] == 0x81, "the CDC notification endpoint is 0x81");
static_assert(VubUsb::Configuration[54] == 1, "the CDC data interface is 1");
static_assert(VubUsb::Configuration[63] == 0x82, "the CDC bulk IN endpoint is 0x82");
static_assert(VubUsb::Configuration[70] == 0x03, "the CDC bulk OUT endpoint is 0x03");
static_assert(USBDescriptorRead16(VubUsb::Configuration, 65) == 64, "the CDC bulk IN packet size is 64");
static_assert(USBDescriptorRead16(VubUsb::Configuration, 72) == 64, "the CDC bulk OUT packet size is 64");

static_assert(
	VubUsb::Configuration[77] == DemoProducts::VubVendorBulkInterface,
	"the vendor bulk interface is DemoProducts::VubVendorBulkInterface"
);
static_assert(
	VubUsb::Configuration[86] == DemoProducts::VubVendorBulkInAddress,
	"the vendor bulk IN endpoint is DemoProducts::VubVendorBulkInAddress"
);
static_assert(
	VubUsb::Configuration[93] == DemoProducts::VubVendorBulkOutAddress,
	"the vendor bulk OUT endpoint is DemoProducts::VubVendorBulkOutAddress"
);
static_assert(USBDescriptorRead16(VubUsb::Configuration, 88) == 64, "the vendor bulk IN packet size is 64");
static_assert(USBDescriptorRead16(VubUsb::Configuration, 95) == 64, "the vendor bulk OUT packet size is 64");

static_assert(VubUsb::Configuration[100] == 3, "the HID interface is 3");
static_assert(VubUsb::Configuration[118] == 0x86, "the HID IN endpoint is 0x86");
static_assert(VubUsb::Configuration[125] == 0x07, "the HID OUT endpoint is 0x07");
static_assert(USBDescriptorRead16(VubUsb::Configuration, 120) == 64, "the HID IN packet size is 64");
static_assert(USBDescriptorRead16(VubUsb::Configuration, 127) == 64, "the HID OUT packet size is 64");

// The MS OS 2.0 vendor code is in the BOS, at 5 + 20 + 6 = 31. The vendor bulk interface's WinUSB binding is the one request of the set,
// answered with wIndex 7.
static_assert(VubUsb::Bos[31] == DemoProducts::VubMsOs20VendorCode, "the BOS must carry DemoProducts::VubMsOs20VendorCode");
static_assert(VubUsb::VendorRequests.size() == 1, "one vendor request, the MS OS 2.0 descriptor");
static_assert(
	VubUsb::VendorRequests[0].request == DemoProducts::VubMsOs20VendorCode,
	"the vendor request must be DemoProducts::VubMsOs20VendorCode"
);
static_assert(VubUsb::VendorRequests[0].index == USBMsOs20DescriptorIndex, "the MS OS 2.0 descriptor is requested with wIndex 7");
static_assert(
	USBDeviceClassMatchesConfiguration(VubUsb::Device, VubUsb::Configuration),
	"device class must match the configuration"
);
static_assert(USBDeviceSupportsBos(VubUsb::Device), "a BOS needs bcdUSB 2.01 or later");
