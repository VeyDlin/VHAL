#pragma once
#include <array>
#include <cstddef>
#include <tuple>
#include <Adapter/Utilities/USBTypes.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorUtility.h>
#include <Drivers/Interface/USB/USBProtocol.h>


// Configuration descriptor and the descriptors it holds: interface (USB 2.0 section 9.6.5), endpoint (9.6.6), interface
// association (USB Interface Association Descriptor ECN) and class-specific or vendor pieces given as bytes. Each builder
// returns one piece of exact size. MakeUSBConfigurationDescriptor() joins them and computes wTotalLength, bNumInterfaces
// and each interface's bNumEndpoints, and it checks the composition as a whole.


// bNumEndpoints is left at 0 here; MakeUSBConfigurationDescriptor() sets it to the endpoints that follow the interface.
constexpr std::array<uint8, 9> MakeUSBInterfaceDescriptor(
	uint8 interfaceNumber,
	uint8 alternateSetting,
	uint8 interfaceClass,
	uint8 interfaceSubClass,
	uint8 interfaceProtocol,
	uint8 stringIndex)
{
	std::array<uint8, 9> bytes{};
	bytes[0] = 9;
	bytes[1] = static_cast<uint8>(USBDescriptorType::Interface);
	bytes[2] = interfaceNumber;
	bytes[3] = alternateSetting;
	bytes[5] = interfaceClass;
	bytes[6] = interfaceSubClass;
	bytes[7] = interfaceProtocol;
	bytes[8] = stringIndex;
	return bytes;
}


// Full-speed limits: bulk packets 8, 16, 32 or 64 bytes with bInterval 0, interrupt packets 1 to 64 bytes. The stack has
// bulk and interrupt endpoints only: isochronous transfers are a non-goal, so their descriptors are rejected.
constexpr std::array<uint8, 7> MakeUSBEndpointDescriptor(
	uint8 address,
	USBEndpointType type,
	uint16 maxPacketSize,
	uint8 interval)
{
	USBDescriptorRequire(USBEndpoint::Number(address) != 0, "endpoint 0 belongs to the control pipe, not to a configuration");
	// Bits 6..4 of bEndpointAddress are reserved and must be zero (USB 2.0 section 9.6.6). USBEndpoint::Number() ignores them,
	// so a set bit would make the descriptor and the controller disagree about the endpoint.
	constexpr uint8 reservedBits = 0x70;
	USBDescriptorRequire((address & reservedBits) == 0, "endpoint address bits 6..4 are reserved");

	std::array<uint8, 7> bytes{};
	bytes[0] = 7;
	bytes[1] = static_cast<uint8>(USBDescriptorType::Endpoint);
	bytes[2] = address;
	if (type == USBEndpointType::Bulk) {
		USBDescriptorRequire(maxPacketSize == 8 || maxPacketSize == 16 || maxPacketSize == 32 || maxPacketSize == 64, "full-speed bulk packets are 8, 16, 32 or 64 bytes");
		USBDescriptorRequire(interval == 0, "bInterval of a full-speed bulk endpoint is 0");
		bytes[3] = 0x02;
	} else {
		USBDescriptorRequire(type == USBEndpointType::Interrupt, "the stack has bulk and interrupt endpoints only");
		USBDescriptorRequire(maxPacketSize >= 1 && maxPacketSize <= 64, "full-speed interrupt packets are 1 to 64 bytes");
		USBDescriptorRequire(interval >= 1, "an interrupt endpoint needs bInterval 1 to 255");
		bytes[3] = 0x03;
	}

	USBDescriptorPut16(bytes, 4, maxPacketSize);
	bytes[6] = interval;
	return bytes;
}


// The audio-class form of the endpoint descriptor (USB Audio 1.0 section 4.8.1.1, also used by USB-MIDI 1.0 for its bulk
// endpoints): the standard seven bytes followed by bRefresh and bSynchAddress, 9 bytes in all. Both are zero for a bulk
// endpoint, which has no refresh rate or synchronisation endpoint. The audio class defines this form for bulk and isochronous
// endpoints only, so an interrupt endpoint is refused here; the address and packet rules are the standard form's.
constexpr std::array<uint8, 9> MakeUSBAudioEndpointDescriptor(
	uint8 address,
	USBEndpointType type,
	uint16 maxPacketSize,
	uint8 interval)
{
	USBDescriptorRequire(type == USBEndpointType::Bulk, "the 9-byte endpoint form is bulk");
	std::array<uint8, 7> standard = MakeUSBEndpointDescriptor(address, type, maxPacketSize, interval);
	std::array<uint8, 9> bytes{};
	for (std::size_t index = 0; index < standard.size(); index++) {
		bytes[index] = standard[index];
	}

	bytes[0] = 9;
	return bytes;
}


// Interface association descriptor: groups firstInterface and the interfaceCount interfaces after it into one function.
constexpr std::array<uint8, 8> MakeUSBInterfaceAssociationDescriptor(
	uint8 firstInterface,
	uint8 interfaceCount,
	uint8 functionClass,
	uint8 functionSubClass,
	uint8 functionProtocol,
	uint8 functionStringIndex)
{
	USBDescriptorRequire(interfaceCount >= 1, "an association covers at least one interface");
	USBDescriptorRequire(static_cast<uint16>(firstInterface) + interfaceCount <= 256, "an association covers interfaces 0 to 255");

	std::array<uint8, 8> bytes{};
	bytes[0] = 8;
	bytes[1] = static_cast<uint8>(USBDescriptorType::InterfaceAssociation);
	bytes[2] = firstInterface;
	bytes[3] = interfaceCount;
	bytes[4] = functionClass;
	bytes[5] = functionSubClass;
	bytes[6] = functionProtocol;
	bytes[7] = functionStringIndex;
	return bytes;
}


// A class-specific or vendor descriptor given as bytes, such as a functional descriptor. bLength is the first byte and must
// equal the array size, so the call is MakeUSBRawDescriptor<Size>({ ... }). The structural types come from their own builders
// and are rejected here, so the composition can count and check them.
template<std::size_t Size>
constexpr std::array<uint8, Size> MakeUSBRawDescriptor(const uint8 (&bytes)[Size]) {
	USBDescriptorRequire(Size >= 2 && Size <= 255, "a descriptor is 2 to 255 bytes");
	USBDescriptorRequire(static_cast<std::size_t>(bytes[0]) == Size, "bLength must equal the size of the array");

	uint8 type = bytes[1];
	USBDescriptorRequire(type != static_cast<uint8>(USBDescriptorType::Configuration), "configuration descriptors come from MakeUSBConfigurationDescriptor");
	USBDescriptorRequire(type != static_cast<uint8>(USBDescriptorType::Interface), "interface descriptors come from MakeUSBInterfaceDescriptor");
	USBDescriptorRequire(type != static_cast<uint8>(USBDescriptorType::Endpoint), "endpoint descriptors come from MakeUSBEndpointDescriptor");
	USBDescriptorRequire(type != static_cast<uint8>(USBDescriptorType::InterfaceAssociation), "association descriptors come from MakeUSBInterfaceAssociationDescriptor");

	std::array<uint8, Size> copy{};
	for (std::size_t index = 0; index < Size; index++) {
		copy[index] = bytes[index];
	}

	return copy;
}


// Device, configuration, string, qualifier and BOS descriptors belong to the whole device, never to one configuration.
// MakeUSBRawDescriptor() rejects them too, but a piece can also be passed as a plain array, so the composer checks here.
constexpr bool USBIsDeviceLevelType(uint8 type) {
	return type == static_cast<uint8>(USBDescriptorType::Device)
		|| type == static_cast<uint8>(USBDescriptorType::Configuration)
		|| type == static_cast<uint8>(USBDescriptorType::String)
		|| type == static_cast<uint8>(USBDescriptorType::DeviceQualifier)
		|| type == static_cast<uint8>(USBDescriptorType::OtherSpeedConfiguration)
		|| type == static_cast<uint8>(USBDescriptorType::BOS)
		|| type == static_cast<uint8>(USBDescriptorType::DeviceCapability);
}


// Checks the composed descriptors after the configuration header and computes the fields that depend on the others: each
// interface's bNumEndpoints (every alternate setting) and the bNumInterfaces it returns (alternate setting 0 only).
template<std::size_t Size>
constexpr uint8 USBFinalizeConfiguration(std::array<uint8, Size>& bytes) {
	std::array<bool, 256> interfaceSeen{};
	std::array<bool, 256> interfaceAssociated{};
	std::array<bool, 256> endpointSeen{};
	std::size_t interfaceCount = 0;
	std::size_t currentInterface = 0;
	bool inInterface = false;
	std::size_t endpointCount = 0;
	std::size_t offset = 9;
	while (offset < Size) {
		USBDescriptorRequire(offset + 2 <= Size, "a descriptor header is cut off");
		std::size_t length = bytes[offset];
		USBDescriptorRequire(length >= 2, "a descriptor is at least 2 bytes");
		USBDescriptorRequire(offset + length <= Size, "a descriptor runs past the configuration");
		uint8 type = bytes[offset + 1];
		USBDescriptorRequire(!USBIsDeviceLevelType(type), "a device-level descriptor cannot be part of a configuration");
		if (type == static_cast<uint8>(USBDescriptorType::Interface)) {
			USBDescriptorRequire(length == 9, "an interface descriptor is 9 bytes");
			if (inInterface) {
				bytes[currentInterface + 4] = static_cast<uint8>(endpointCount);
			}

			uint8 number = bytes[offset + 2];
			uint8 alternate = bytes[offset + 3];
			if (alternate == 0) {
				USBDescriptorRequire(!interfaceSeen[number], "interface numbers are unique");
				interfaceSeen[number] = true;
				interfaceCount++;
			} else {
				USBDescriptorRequire(interfaceSeen[number], "an alternate setting follows its default setting");
			}

			currentInterface = offset;
			inInterface = true;
			endpointCount = 0;
		} else if (type == static_cast<uint8>(USBDescriptorType::Endpoint)) {
			USBDescriptorRequire(inInterface, "an endpoint follows an interface");
			USBDescriptorRequire(length == 7 || length == 9, "an endpoint descriptor is 7 bytes, or 9 bytes in the audio form");
			// Bits 6..4 of bEndpointAddress are reserved, as MakeUSBEndpointDescriptor() checks. A plain array gets the same rule here.
			USBDescriptorRequire((bytes[offset + 2] & 0x70) == 0, "endpoint address bits 6..4 are reserved");
			if (length == 9) {
				// The audio form adds bRefresh and bSynchAddress, which only isochronous endpoints use. Isochronous transfers are
				// not supported, and the audio class defines no 9-byte interrupt endpoint, so the form is bulk with both fields zero.
				uint8 transferType = bytes[offset + 3] & 0x03;
				USBDescriptorRequire(transferType == 0x02, "the 9-byte endpoint form is bulk");
				USBDescriptorRequire(bytes[offset + 7] == 0 && bytes[offset + 8] == 0, "bRefresh and bSynchAddress are 0 for bulk endpoints");
			}

			uint8 address = bytes[offset + 2];
			USBDescriptorRequire(!endpointSeen[address], "endpoint addresses are unique in the configuration");
			endpointSeen[address] = true;
			endpointCount++;
		} else if (type == static_cast<uint8>(USBDescriptorType::InterfaceAssociation)) {
			USBDescriptorRequire(length == 8, "an interface association descriptor is 8 bytes");
			std::size_t first = bytes[offset + 2];
			std::size_t count = bytes[offset + 3];
			USBDescriptorRequire(count >= 1 && first + count <= 256, "an association covers one or more interfaces from 0 to 255");
			for (std::size_t number = first; number < first + count; number++) {
				USBDescriptorRequire(!interfaceSeen[number], "an association precedes the interfaces it covers");
				USBDescriptorRequire(!interfaceAssociated[number], "an interface belongs to one association");
				interfaceAssociated[number] = true;
			}
		}

		offset += length;
	}

	if (inInterface) {
		bytes[currentInterface + 4] = static_cast<uint8>(endpointCount);
	}

	USBDescriptorRequire(interfaceCount >= 1, "a configuration has at least one interface");
	USBDescriptorRequire(interfaceCount <= 255, "bNumInterfaces is one byte");
	for (std::size_t number = 0; number < 256; number++) {
		if (interfaceAssociated[number]) {
			USBDescriptorRequire(interfaceSeen[number], "an association covers an interface the configuration lacks");
		}
	}

	return static_cast<uint8>(interfaceCount);
}


// The configuration descriptor (USB 2.0 section 9.6.3) and the pieces in the order given. Each endpoint must follow the
// interface it belongs to. maxPower is in units of 2 mA, as the wire field, and at most 250 (500 mA).
template<typename... Pieces>
constexpr std::array<uint8, 9 + (std::tuple_size_v<Pieces> + ... + 0)> MakeUSBConfigurationDescriptor(
	uint8 configurationValue,
	uint8 stringIndex,
	bool selfPowered,
	bool remoteWakeup,
	uint8 maxPower,
	const Pieces&... pieces)
{
	constexpr std::size_t total = 9 + (std::tuple_size_v<Pieces> + ... + 0);
	USBDescriptorRequire(configurationValue >= 1, "bConfigurationValue 0 means not configured");
	USBDescriptorRequire(total <= 0xFFFF, "wTotalLength is 16 bits");
	USBDescriptorRequire(maxPower <= 250, "bMaxPower is at most 500 mA");

	std::array<uint8, total> bytes{};
	std::size_t offset = 9;
	(static_cast<void>(offset = USBDescriptorAppend(bytes, offset, pieces)), ...);

	uint8 interfaceCount = USBFinalizeConfiguration(bytes);
	uint8 attributes = 0x80;
	if (selfPowered) {
		attributes |= 0x40;
	}

	if (remoteWakeup) {
		attributes |= 0x20;
	}

	bytes[0] = 9;
	bytes[1] = static_cast<uint8>(USBDescriptorType::Configuration);
	USBDescriptorPut16(bytes, 2, static_cast<uint16>(total));
	bytes[4] = interfaceCount;
	bytes[5] = configurationValue;
	bytes[6] = stringIndex;
	bytes[7] = attributes;
	bytes[8] = maxPower;
	return bytes;
}


// True when the configuration holds an interface association. Such a device uses device class 0xEF, subclass 0x02, protocol
// 0x01. The bytes are input, so the walk checks each length as USBFinalizeConfiguration() does. It walks the whole array, so
// a malformed descriptor after the association is rejected too.
template<std::size_t Size>
constexpr bool USBConfigurationUsesAssociation(const std::array<uint8, Size>& configuration) {
	USBDescriptorRequire(Size >= 9, "a configuration descriptor is at least 9 bytes");
	USBDescriptorRequire(configuration[1] == static_cast<uint8>(USBDescriptorType::Configuration), "the bytes are a configuration descriptor");
	USBDescriptorRequire(USBDescriptorRead16(configuration, 2) == Size, "wTotalLength must equal the configuration's size");
	bool associated = false;
	std::size_t offset = 9;
	while (offset < Size) {
		USBDescriptorRequire(offset + 2 <= Size, "a descriptor header is cut off");
		std::size_t length = configuration[offset];
		USBDescriptorRequire(length >= 2, "a descriptor is at least 2 bytes");
		USBDescriptorRequire(offset + length <= Size, "a descriptor runs past the configuration");
		if (configuration[offset + 1] == static_cast<uint8>(USBDescriptorType::InterfaceAssociation)) {
			associated = true;
		}

		offset += length;
	}

	return associated;
}
