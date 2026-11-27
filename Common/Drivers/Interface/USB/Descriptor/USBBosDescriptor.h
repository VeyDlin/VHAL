#pragma once
#include <array>
#include <cstddef>
#include <span>
#include <tuple>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorUtility.h>
#include <Drivers/Interface/USB/USBProtocol.h>
#include <Utilities/CompileTime/Require.h>


// bDevCapabilityType values of the device capabilities in a BOS descriptor (USB 3.x).
enum class USBDeviceCapabilityType : uint8 {
	Usb2Extension = 0x02,
	Platform = 0x05
};


// USB 2.0 Extension capability. Bit 1 of bmAttributes reports link power management (LPM) support, the one bit defined.
constexpr std::array<uint8, 7> MakeUSBUsb2ExtensionCapability(bool linkPowerManagement) {
	std::array<uint8, 7> bytes{};
	bytes[0] = 7;
	bytes[1] = static_cast<uint8>(USBDescriptorType::DeviceCapability);
	bytes[2] = static_cast<uint8>(USBDeviceCapabilityType::Usb2Extension);
	if (linkPowerManagement) {
		bytes[3] = 0x02;
	}

	return bytes;
}


// Platform capability: a 16-byte UUID that names the platform, then platform-defined data. The data is passed through
// unchanged, because its layout belongs to the platform.
template<std::size_t DataSize>
constexpr std::array<uint8, 20 + DataSize> MakeUSBPlatformCapability(
	const std::array<uint8, 16>& platformUuid,
	const std::array<uint8, DataSize>& capabilityData)
{
	Require<"USB: a platform capability is at most 255 bytes">(20 + DataSize <= 255);

	std::array<uint8, 20 + DataSize> bytes{};
	bytes[0] = static_cast<uint8>(20 + DataSize);
	bytes[1] = static_cast<uint8>(USBDescriptorType::DeviceCapability);
	bytes[2] = static_cast<uint8>(USBDeviceCapabilityType::Platform);
	bytes[3] = 0;
	USBDescriptorAppend(bytes, 4, platformUuid);
	USBDescriptorAppend(bytes, 20, capabilityData);
	return bytes;
}


// Appends one device capability to a BOS descriptor. The capability must be one (type 16) whose bLength is its size.
template<std::size_t Size>
constexpr std::size_t USBBosAppendCapability(
	std::array<uint8, Size>& bytes,
	std::size_t offset,
	std::span<const uint8> capability)
{
	Require<"USB: a device capability is at least 3 bytes">(capability.size() >= 3);
	Require<"USB: a device capability's bLength must equal its size">(static_cast<std::size_t>(capability[0]) == capability.size());
	Require<"USB: a BOS entry is a device capability">(capability[1] == static_cast<uint8>(USBDescriptorType::DeviceCapability));
	return USBDescriptorAppend(bytes, offset, capability);
}


// The BOS descriptor (USB 3.x): a 5-byte header, then the device capabilities in the order given. wTotalLength and
// bNumDeviceCaps are computed from them.
template<typename... Capabilities>
constexpr std::array<uint8, 5 + (std::tuple_size_v<Capabilities> + ... + 0)> MakeUSBBosDescriptor(const Capabilities&... capabilities) {
	constexpr std::size_t total = 5 + (std::tuple_size_v<Capabilities> + ... + 0);
	Require<"USB: a BOS descriptor holds at least one device capability">(sizeof...(Capabilities) >= 1);
	Require<"USB: bNumDeviceCaps is one byte">(sizeof...(Capabilities) <= 255);
	Require<"USB: wTotalLength is 16 bits">(total <= 0xFFFF);

	std::array<uint8, total> bytes{};
	bytes[0] = 5;
	bytes[1] = static_cast<uint8>(USBDescriptorType::BOS);
	USBDescriptorPut16(bytes, 2, static_cast<uint16>(total));
	bytes[4] = static_cast<uint8>(sizeof...(Capabilities));
	std::size_t offset = 5;
	(static_cast<void>(offset = USBBosAppendCapability(bytes, offset, capabilities)), ...);
	return bytes;
}
