#pragma once
#include <VHAL.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorSet.h>
#include <span>


// The descriptor bytes of the host tests, written out by hand from USB 2.0 chapter 9 and the Microsoft OS 2.0 descriptor
// specification. They are deliberately not produced by the Descriptor builders: the tests compare the core's answers with
// these arrays, so a builder mistake cannot hide in both. The identity is the pid.codes test VID/PID (0x1209/0x0001).
namespace SimDescriptors {


// Device descriptor, 18 bytes. Class is per interface, bMaxPacketSize0 is 64, two strings, one configuration.
inline constexpr uint8 DeviceDescriptor[] = {
	0x12, 0x01,				// bLength 18, bDescriptorType 1
	0x01, 0x02,				// bcdUSB 2.01: a device with a BOS must say 2.01 or later (USB 2.0 LPM ECN), so 2.01 is the lowest it can be
	0x00, 0x00, 0x00,		// bDeviceClass, bDeviceSubClass, bDeviceProtocol
	0x40,					// bMaxPacketSize0 64
	0x09, 0x12,				// idVendor 0x1209
	0x01, 0x00,				// idProduct 0x0001
	0x00, 0x01,				// bcdDevice 1.00
	0x01, 0x02, 0x03,		// iManufacturer, iProduct, iSerialNumber
	0x01					// bNumConfigurations
};


static_assert(sizeof(DeviceDescriptor) == 18, "device descriptor is 18 bytes");


// One interface with a bulk IN 0x81 and a bulk OUT 0x01, both 64 bytes. wTotalLength 32.
inline constexpr uint8 ConfigurationDescriptor[] = {
	0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,	// configuration: 32 bytes, 1 interface, value 1, bus powered, 100 mA
	0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00,	// interface 0: vendor class, 2 endpoints
	0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,				// endpoint 0x81: bulk IN, 64 bytes
	0x07, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00				// endpoint 0x01: bulk OUT, 64 bytes
};


static_assert(sizeof(ConfigurationDescriptor) == 32, "configuration is 32 bytes");


// The same configuration with remote wakeup advertised (bmAttributes bit 5). A device accepts SET_FEATURE(DEVICE_REMOTE_WAKEUP)
// only when its configuration says it supports the feature (USB 2.0 9.4.9).
inline constexpr uint8 RemoteWakeupConfiguration[] = {
	0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0xA0, 0x32,	// configuration: 32 bytes, 1 interface, value 1, bus powered, remote wakeup, 100 mA
	0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00,	// interface 0: vendor class, 2 endpoints
	0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,				// endpoint 0x81: bulk IN, 64 bytes
	0x07, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00				// endpoint 0x01: bulk OUT, 64 bytes
};


static_assert(sizeof(RemoteWakeupConfiguration) == 32, "configuration is 32 bytes");


// Two interfaces, each with its own bulk pair (0x81/0x01 and 0x82/0x02). wTotalLength 55.
inline constexpr uint8 CompositeConfiguration[] = {
	0x09, 0x02, 0x37, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,	// configuration: 55 bytes, 2 interfaces, value 1
	0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00,	// interface 0
	0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,				// endpoint 0x81: bulk IN
	0x07, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00,				// endpoint 0x01: bulk OUT
	0x09, 0x04, 0x01, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00,	// interface 1
	0x07, 0x05, 0x82, 0x02, 0x40, 0x00, 0x00,				// endpoint 0x82: bulk IN
	0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00				// endpoint 0x02: bulk OUT
};


static_assert(sizeof(CompositeConfiguration) == 55, "composite configuration is 55 bytes");


// strings[0]: the LANGID list, English (United States) 0x0409.
inline constexpr uint8 StringLanguage[] = {
	0x04, 0x03, 0x09, 0x04
};


// strings[1]: "Sim" as UTF-16LE.
inline constexpr uint8 StringManufacturer[] = {
	0x08, 0x03, 0x53, 0x00, 0x69, 0x00, 0x6D, 0x00
};


// strings[2]: "Bulk" as UTF-16LE.
inline constexpr uint8 StringProduct[] = {
	0x0A, 0x03, 0x42, 0x00, 0x75, 0x00, 0x6C, 0x00, 0x6B, 0x00
};


// strings[3]: "1" as UTF-16LE.
inline constexpr uint8 StringSerial[] = {
	0x04, 0x03, 0x31, 0x00
};


inline constexpr std::span<const uint8> StringTable[] = {
	StringLanguage,
	StringManufacturer,
	StringProduct,
	StringSerial
};


// BOS: header (wTotalLength 40, two device capabilities), the USB 2.0 extension, and the Microsoft OS 2.0 platform
// capability. The MS OS 2.0 platform UUID is {D8DD60DF-4589-4CC7-9CD2-659D9E648A9F}, stored with its first three fields in
// little-endian order.
inline constexpr uint8 BosDescriptor[] = {
	0x05, 0x0F, 0x28, 0x00, 0x02,							// BOS descriptor, wTotalLength 40, bNumDeviceCaps 2
	0x07, 0x10, 0x02, 0x00, 0x00, 0x00, 0x00,				// USB 2.0 extension, LPM bit clear: the stack does not implement LPM (a non-goal)
	0x1C, 0x10, 0x05, 0x00,									// platform capability, 28 bytes, MS OS 2.0
	0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C,			// platform capability UUID
	0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F,
	0x00, 0x00, 0x03, 0x06,									// dwWindowsVersion 0x06030000 (Windows 8.1)
	0x1E, 0x00,												// wMSOSDescriptorSetTotalLength 30
	0x42,													// bMS_VendorCode
	0x00													// bAltEnumCode
};


static_assert(sizeof(BosDescriptor) == 40, "BOS is 40 bytes");


// MS OS 2.0 descriptor set: the set header, then one compatible ID feature binding WINUSB. wTotalLength 30.
inline constexpr uint8 MsOs20Set[] = {
	0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x06, 0x1E, 0x00,		// set header
	0x14, 0x00, 0x03, 0x00,											// compatible ID feature, 20 bytes
	0x57, 0x49, 0x4E, 0x55, 0x53, 0x42, 0x00, 0x00,					// CompatibleID "WINUSB"
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00					// SubCompatibleID
};


static_assert(sizeof(MsOs20Set) == 30, "MS OS 2.0 set is 30 bytes");


// 64 bytes of vendor data that counts from 0x00 to 0x3F, so a test can compare every byte.
inline constexpr uint8 VendorBlob64[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
	0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F
};


static_assert(sizeof(VendorBlob64) == 64, "vendor blob is 64 bytes");


// Vendor requests to the device: 0x42 with wIndex 7 is the MS OS 2.0 descriptor, 0x50 with wIndex 0 is the 64-byte blob.
inline constexpr USBVendorDescriptor VendorRequests[] = {
	{ 0x42, 7, MsOs20Set },
	{ 0x50, 0, VendorBlob64 }
};


inline constexpr USBDescriptorSet SimpleSet = {
	DeviceDescriptor,
	ConfigurationDescriptor,
	StringTable,
	BosDescriptor,
	VendorRequests
};


// The simple set with remote wakeup advertised in its configuration.
inline constexpr USBDescriptorSet RemoteWakeupSet = {
	DeviceDescriptor,
	RemoteWakeupConfiguration,
	StringTable,
	BosDescriptor,
	VendorRequests
};


// The same configuration with the OUT on 0x02 instead of 0x01, so the endpoint numbers are 1 and 2 (TestVendorBulk's separate-number pair).
inline constexpr uint8 SeparateNumberConfiguration[] = {
	0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,	// configuration: 32 bytes, 1 interface, value 1, bus powered, 100 mA
	0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00,	// interface 0: vendor class, 2 endpoints
	0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,				// endpoint 0x81: bulk IN, 64 bytes
	0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00				// endpoint 0x02: bulk OUT, 64 bytes
};


static_assert(sizeof(SeparateNumberConfiguration) == 32, "configuration is 32 bytes");


// The simple set with the separate-number configuration. Its endpoints are the ones the vendor function opens on 0x81 and 0x02.
inline constexpr USBDescriptorSet SeparateNumberSet = {
	DeviceDescriptor,
	SeparateNumberConfiguration,
	StringTable,
	BosDescriptor,
	VendorRequests
};


// The composite configuration with the same device and strings and no BOS or vendor requests.
inline constexpr USBDescriptorSet CompositeSet = {
	DeviceDescriptor,
	CompositeConfiguration,
	StringTable,
	std::span<const uint8>(),
	std::span<const USBVendorDescriptor>()
};


}
