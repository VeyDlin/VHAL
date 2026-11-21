#pragma once
#include <DemoProducts.h>
#include <Drivers/Interface/USB/Class/CDC/USBCdcNcmBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBConfigurationBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorComposition.h>


// The descriptors of the network device: one CDC-NCM function, a communication interface with the notification endpoint 0x83 and the
// data interface with the bulk IN 0x81 and bulk OUT 0x02. The in-box drivers of Windows 10 2004 and later, Linux (cdc_ncm) and macOS
// bind to the class codes, so there is no INF and no WinUSB.
//
// The order of the calls sets the string indexes: manufacturer 1, product 2, the serial number 3 (filled in at run time from the MCU's
// unique ID), the communication interface's name 4, and the run-time MAC string 5 (iMACAddress, given by SetRuntimeString()).
using NetworkUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(DemoProducts::VendorId)
		.ProductId(DemoProducts::NetworkProductId)
		.DeviceRelease(DemoProducts::DeviceRelease)
		.Manufacturer("VHAL")
		.Product("NCM Network Demo")
		.SerialNumberAtRuntime()
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBCdcNcmBuilder()
				.Name("Ethernet over USB")
				.NotificationIn(0x83)
				.BulkIn(0x81)
				.BulkOut(0x02)));
}>;


// The device descriptor's identity and the configuration's size and endpoints, checked against the shared IDs and the NCM 1.0 layout.
// The configuration is 94 bytes: the header (9), the interface association (8), the communication interface (9), the header, union,
// Ethernet and NCM functional descriptors (5, 5, 13, 6), the notification endpoint (7), the data interface's two settings (9 and 9) and
// the bulk endpoints (7 each). The notification endpoint's address is at 57, the bulk IN's at 82 and the bulk OUT's at 89.
static_assert(USBDescriptorRead16(NetworkUsb::Device, 8) == DemoProducts::VendorId, "idVendor must be DemoProducts::VendorId");
static_assert(USBDescriptorRead16(NetworkUsb::Device, 10) == DemoProducts::NetworkProductId, "idProduct must be DemoProducts::NetworkProductId");
static_assert(USBDescriptorRead16(NetworkUsb::Device, 12) == DemoProducts::DeviceRelease, "bcdDevice must be DemoProducts::DeviceRelease");
static_assert(USBDescriptorRead16(NetworkUsb::Configuration, 2) == 94, "the configuration is 94 bytes");
static_assert(NetworkUsb::Configuration[4] == 2, "the configuration has two interfaces");
static_assert(NetworkUsb::Configuration[57] == 0x83, "the notification endpoint is 0x83");
static_assert(NetworkUsb::Configuration[82] == 0x81, "the bulk IN endpoint is 0x81");
static_assert(NetworkUsb::Configuration[89] == 0x02, "the bulk OUT endpoint is 0x02");
