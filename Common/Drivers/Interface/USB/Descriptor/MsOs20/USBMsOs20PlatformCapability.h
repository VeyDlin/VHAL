#pragma once
#include <array>
#include <cstddef>
#include <Drivers/Interface/USB/Descriptor/USBBosDescriptor.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorUtility.h>
#include <Utilities/CompileTime/Require.h>


// Optional component. Microsoft OS 2.0 descriptors let Windows bind WinUSB without an INF file; other hosts ignore them.
// Every value comes from the application: the Windows version and the vendor request are passed in or read from the set.


// Platform capability UUID of the Microsoft OS 2.0 descriptors, D8DD60DF-4589-4CC7-9CD2-659D9E648A9F, in wire byte order (the
// first three fields are little-endian). It is a fixed value of the Microsoft specification, not an identity of the device.
constexpr std::array<uint8, 16> USBMsOs20PlatformUuid{
	0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C, 0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F
};


// The 28-byte BOS platform capability for a descriptor set. dwWindowsVersion and wMSOSDescriptorSetTotalLength are read from
// the set itself, so the capability cannot disagree with it. vendorCode is the bRequest the host sends to fetch the set
// (wIndex 7); alternateEnumerationCode is bAltEnumCode, 0 when the device does not use alternate enumeration.
template<std::size_t SetSize>
constexpr std::array<uint8, 28> MakeMsOs20PlatformCapability(
	const std::array<uint8, SetSize>& descriptorSet,
	uint8 vendorCode,
	uint8 alternateEnumerationCode)
{
	Require<"USB: a descriptor set is 10 to 65535 bytes">(SetSize >= 10 && SetSize <= 0xFFFF);
	Require<"USB: the set starts with a 10-byte set header">(USBDescriptorRead16(descriptorSet, 0) == 10);
	Require<"USB: the set header's wTotalLength must equal the set's size">(USBDescriptorRead16(descriptorSet, 8) == SetSize);

	std::array<uint8, 8> data{};
	for (std::size_t index = 0; index < 4; index++) {
		data[index] = descriptorSet[4 + index];
	}

	USBDescriptorPut16(data, 4, static_cast<uint16>(SetSize));
	data[6] = vendorCode;
	data[7] = alternateEnumerationCode;
	return MakeUSBPlatformCapability(USBMsOs20PlatformUuid, data);
}
