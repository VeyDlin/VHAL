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


// The report descriptors of the boot keyboard and the boot mouse. The HID functions take the same Info as their descriptors, so the bytes
// the host reads and the bytes the functions answer with come from one object.
using CompositeKeyboardReport = USBHidReportDescriptor<+[] { return USBHidReportBuilder::BootKeyboard(); }>;
using CompositeMouseReport = USBHidReportDescriptor<+[] { return USBHidReportBuilder::BootMouse(); }>;


// The composite device: a keyboard, a mouse, a serial port (CDC-ACM) and a vendor raw channel, in that order. The functions take their
// interface numbers in that order: keyboard 0, mouse 1, serial port 2 and 3, vendor channel 4. The endpoints are numbered by the builder,
// one number per endpoint, so IN and OUT never share a number. Seven endpoints take numbers 1 to 7, which is the EndpointNumberLimit().
// The map is in DemoProducts.h and the static asserts below check it.
//
// The string indexes are: manufacturer 1, product 2, the serial number 3 (filled in at run time) and the function names 4 to 7.
using CompositeUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(DemoProducts::VendorId)
		.ProductId(DemoProducts::CompositeProductId)
		.DeviceRelease(DemoProducts::DeviceRelease)
		.Manufacturer("VHAL")
		.Product("USB Composite")
		.SerialNumberAtRuntime()
		.MsOs20VendorCode(DemoProducts::CompositeMsOs20VendorCode)
		.EndpointNumberLimit(7)
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBHidBuilder()
				.BootKeyboard()
				.Name("Keyboard")
				.Report(CompositeKeyboardReport::Info))
			.Function(USBHidBuilder()
				.BootMouse()
				.Name("Mouse")
				.Report(CompositeMouseReport::Info))
			.Function(USBCdcAcmBuilder()
				.Name("Serial"))
			.Function(USBVendorBuilder()
				.Name("Vendor raw")
				.WinUsb(DemoProducts::CompositeInterfaceGuid)));
}>;


// Offsets in the configuration: the header is 9 bytes, then the functions in order. Keyboard: interface 9 (bInterfaceNumber at 11),
// HID descriptor 18, endpoint 27 (address at 29, wMaxPacketSize at 31). Mouse: interface 34 (number at 36), HID 43, endpoint 52 (address
// at 54, size at 56). Serial port: interface association 59, communication interface 67 (number at 69), notification endpoint 95 (address
// at 97), data interface 102 (number at 104), bulk IN 111 (address at 113, size at 115), bulk OUT 118 (address at 120, size at 122).
// Vendor channel: interface 125 (number at 127), bulk IN 134 (address at 136), bulk OUT 141 (address at 143).
// The configuration is 148 bytes.
// The offsets are the same as the bench's, which the bench asserts in the same way.
static_assert(USBDescriptorRead16(CompositeUsb::Device, 8) == DemoProducts::VendorId, "idVendor must be DemoProducts::VendorId");
static_assert(
	USBDescriptorRead16(CompositeUsb::Device, 10) == DemoProducts::CompositeProductId,
	"idProduct must be DemoProducts::CompositeProductId"
);
static_assert(
	USBDescriptorRead16(CompositeUsb::Device, 12) == DemoProducts::DeviceRelease,
	"bcdDevice must be DemoProducts::DeviceRelease"
);
static_assert(USBDescriptorRead16(CompositeUsb::Configuration, 2) == 148, "the configuration is 148 bytes");
// The report descriptors are 64 and 56 bytes, with a data byte on every item but End Collection (Windows rejects the zero-size form).
// The HID descriptors carry those lengths at offset 25 (keyboard) and 50 (mouse), the wDescriptorLength of each.
static_assert(CompositeKeyboardReport::Info.descriptor.size() == 64, "the keyboard report descriptor is 64 bytes");
static_assert(CompositeMouseReport::Info.descriptor.size() == 56, "the mouse report descriptor is 56 bytes");
static_assert(USBDescriptorRead16(CompositeUsb::Configuration, 25) == 64, "the keyboard's HID descriptor gives a 64-byte report descriptor");
static_assert(USBDescriptorRead16(CompositeUsb::Configuration, 50) == 56, "the mouse's HID descriptor gives a 56-byte report descriptor");
static_assert(CompositeUsb::Configuration[4] == 5, "the configuration has five interfaces");

static_assert(CompositeUsb::Configuration[11] == 0, "the keyboard is interface 0");
static_assert(CompositeUsb::Configuration[29] == 0x81, "the keyboard's IN endpoint is 0x81");
static_assert(USBDescriptorRead16(CompositeUsb::Configuration, 31) == 8, "the keyboard's packet size is 8, the boot report");

static_assert(CompositeUsb::Configuration[36] == 1, "the mouse is interface 1");
static_assert(CompositeUsb::Configuration[54] == 0x82, "the mouse's IN endpoint is 0x82");
static_assert(USBDescriptorRead16(CompositeUsb::Configuration, 56) == 4, "the mouse's packet size is 4, the report with the wheel");

static_assert(CompositeUsb::Configuration[69] == 2, "the serial port's communication interface is 2");
static_assert(CompositeUsb::Configuration[97] == 0x83, "the serial port's notification endpoint is 0x83");
static_assert(CompositeUsb::Configuration[104] == 3, "the serial port's data interface is 3");
static_assert(CompositeUsb::Configuration[113] == 0x84, "the serial port's bulk IN endpoint is 0x84");
static_assert(CompositeUsb::Configuration[120] == 0x05, "the serial port's bulk OUT endpoint is 0x05");
static_assert(USBDescriptorRead16(CompositeUsb::Configuration, 115) == 64, "the serial port's bulk IN packet size is 64");
static_assert(USBDescriptorRead16(CompositeUsb::Configuration, 122) == 64, "the serial port's bulk OUT packet size is 64");

static_assert(
	CompositeUsb::Configuration[127] == DemoProducts::CompositeVendorInterface,
	"the vendor channel is DemoProducts::CompositeVendorInterface"
);
static_assert(
	CompositeUsb::Configuration[136] == DemoProducts::CompositeVendorBulkInAddress,
	"the vendor channel's bulk IN is DemoProducts::CompositeVendorBulkInAddress"
);
static_assert(
	CompositeUsb::Configuration[143] == DemoProducts::CompositeVendorBulkOutAddress,
	"the vendor channel's bulk OUT is DemoProducts::CompositeVendorBulkOutAddress"
);
static_assert(USBDescriptorRead16(CompositeUsb::Configuration, 138) == 64, "the vendor channel's bulk IN packet size is 64");
static_assert(USBDescriptorRead16(CompositeUsb::Configuration, 145) == 64, "the vendor channel's bulk OUT packet size is 64");

// The MS OS 2.0 vendor code is in the BOS, at 5 + 20 + 6 = 31 (see BenchDescriptors.h for the layout). The vendor channel's WinUSB
// binding is the one request of the set, answered with wIndex 7.
static_assert(
	CompositeUsb::Bos[31] == DemoProducts::CompositeMsOs20VendorCode,
	"the BOS must carry DemoProducts::CompositeMsOs20VendorCode"
);
static_assert(CompositeUsb::VendorRequests.size() == 1, "one vendor request, the MS OS 2.0 descriptor");
static_assert(
	CompositeUsb::VendorRequests[0].request == DemoProducts::CompositeMsOs20VendorCode,
	"the vendor request must be DemoProducts::CompositeMsOs20VendorCode"
);
static_assert(CompositeUsb::VendorRequests[0].index == USBMsOs20DescriptorIndex, "the MS OS 2.0 descriptor is requested with wIndex 7");
static_assert(
	USBDeviceClassMatchesConfiguration(CompositeUsb::Device, CompositeUsb::Configuration),
	"device class must match the configuration"
);
static_assert(USBDeviceSupportsBos(CompositeUsb::Device), "a BOS needs bcdUSB 2.01 or later");
