#pragma once
#include <array>
#include <cstddef>
#include <span>
#include <Drivers/Interface/USB/Descriptor/USBBosDescriptor.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorSet.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorUtility.h>


// Optional component: the WebUSB platform capability in the BOS, and the GET_URL vendor request that answers with the landing
// page. Browsers that implement WebUSB read the capability and then ask for the URL; other hosts ignore both.


// wIndex of the GET_URL request. Fixed by the WebUSB specification.
constexpr uint16 USBWebUsbRequestIndex = 2;

// bcdVersion of the WebUSB platform capability, 1.00.
constexpr uint16 USBWebUsbVersion = 0x0100;

// URL index of the landing page when the device has one URL, which is the case for the device builder. Index 0 means no
// landing page, so the first URL descriptor is 1.
constexpr uint8 USBWebUsbLandingPageIndex = 1;

// bDevCapabilityType is 5 (platform); the UUID names WebUSB. Wire byte order: the first three fields are little-endian, so
// {3408B638-09A9-47A0-8BFD-A0768815B665} is stored as below.
constexpr std::array<uint8, 16> USBWebUsbPlatformUuid{
	0x38, 0xB6, 0x08, 0x34, 0xA9, 0x09, 0xA0, 0x47, 0x8B, 0xFD, 0xA0, 0x76, 0x88, 0x15, 0xB6, 0x65
};


// The 24-byte WebUSB platform capability: bcdVersion 0x0100, the vendor code the host sends GET_URL with, and the index of the
// landing page URL descriptor (1 for the single URL the builder takes; 0 means the device has no landing page).
constexpr std::array<uint8, 24> MakeWebUsbPlatformCapability(uint8 vendorCode, uint8 landingPageIndex) {
	std::array<uint8, 4> data{};
	USBDescriptorPut16(data, 0, USBWebUsbVersion);
	data[2] = vendorCode;
	data[3] = landingPageIndex;
	return MakeUSBPlatformCapability(USBWebUsbPlatformUuid, data);
}


// The vendor request entry for GET_URL: bRequest is the vendor code, wIndex is 2, and wValue must name the landing page index.
// Any other wValue is stalled by the core, because the request is matched on wValue too. The span must refer to storage that
// lives as long as the device (static constexpr).
constexpr USBVendorDescriptor MakeWebUsbVendorDescriptor(uint8 vendorCode, uint8 landingPageIndex, std::span<const uint8> url) {
	return USBVendorDescriptor{ vendorCode, USBWebUsbRequestIndex, url, landingPageIndex, true };
}
