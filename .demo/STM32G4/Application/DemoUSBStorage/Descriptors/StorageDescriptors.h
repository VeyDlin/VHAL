#pragma once
#include <DemoProducts.h>
#include <Drivers/Interface/USB/Class/MSC/USBMscBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBConfigurationBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorComposition.h>


// The descriptors of the storage device: one mass storage interface (Bulk-Only Transport, SCSI transparent) with a bulk IN 0x81 and a
// bulk OUT 0x02. Windows, Linux and macOS bind their in-box drivers to it, so there is no WinUSB and no MS OS 2.0 descriptor.
//
// The order of the calls sets the string indexes: manufacturer 1, product 2, the serial number 3 (filled in at run time by
// USBDevice::SetSerialNumber() from the MCU's unique ID) and the interface name 4.
using StorageUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(DemoProducts::VendorId)
		.ProductId(DemoProducts::StorageProductId)
		.DeviceRelease(DemoProducts::DeviceRelease)
		.Manufacturer(DemoProducts::StorageVendor)
		.Product(DemoProducts::StorageProduct)
		.SerialNumberAtRuntime()
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBMscBuilder()
				.Name("Mass storage")
				.BulkIn(0x81)
				.BulkOut(0x02)));
}>;


// Offsets in the configuration: the header is 9 bytes, then the interface (9 bytes) and two endpoints (7 bytes each). The interface is
// at 9, its class at 14 (0x08, mass storage), subclass 15 (0x06, SCSI transparent) and protocol 16 (0x50, Bulk-Only). The bulk IN endpoint
// is at 18 with its address at 20 and its size at 22, the bulk OUT endpoint at 25 with its address at 27 and its size at 29.
static_assert(USBDescriptorRead16(StorageUsb::Device, 8) == DemoProducts::VendorId, "idVendor must be DemoProducts::VendorId");
static_assert(USBDescriptorRead16(StorageUsb::Device, 10) == DemoProducts::StorageProductId, "idProduct must be DemoProducts::StorageProductId");
static_assert(USBDescriptorRead16(StorageUsb::Device, 12) == DemoProducts::DeviceRelease, "bcdDevice must be DemoProducts::DeviceRelease");
static_assert(USBDescriptorRead16(StorageUsb::Configuration, 2) == 32, "the configuration is 32 bytes");
static_assert(StorageUsb::Configuration[4] == 1, "the configuration has one interface");
static_assert(StorageUsb::Configuration[14] == 0x08, "the interface is class 0x08, mass storage");
static_assert(StorageUsb::Configuration[15] == 0x06, "the interface is subclass 0x06, SCSI transparent");
static_assert(StorageUsb::Configuration[16] == 0x50, "the interface is protocol 0x50, Bulk-Only");
static_assert(StorageUsb::Configuration[20] == 0x81, "the bulk IN endpoint is 0x81");
static_assert(USBDescriptorRead16(StorageUsb::Configuration, 22) == 64, "the bulk IN packet size is 64");
static_assert(StorageUsb::Configuration[27] == 0x02, "the bulk OUT endpoint is 0x02");
static_assert(USBDescriptorRead16(StorageUsb::Configuration, 29) == 64, "the bulk OUT packet size is 64");
