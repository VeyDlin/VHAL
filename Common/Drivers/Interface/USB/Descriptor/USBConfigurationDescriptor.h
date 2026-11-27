#pragma once
#include <array>
#include <cstddef>
#include <tuple>
#include <Adapter/Utilities/USBTypes.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorUtility.h>
#include <Drivers/Interface/USB/USBProtocol.h>
#include <Utilities/CompileTime/Require.h>


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


// Synchronisation type of an isochronous endpoint: bmAttributes bits 3..2 (USB 2.0 section 9.6.6). Bulk and interrupt endpoints
// have none, so they keep None.
enum class USBIsoSync : uint8 {
	None = 0,
	Asynchronous = 1,
	Adaptive = 2,
	Synchronous = 3
};


// Usage of an isochronous endpoint: bmAttributes bits 5..4. A feedback endpoint carries the rate the data endpoint is paced by.
enum class USBIsoUsage : uint8 {
	Data = 0,
	Feedback = 1,
	ImplicitFeedbackData = 2
};


// Full-speed limits: bulk packets 8, 16, 32 or 64 bytes with bInterval 0, interrupt packets 1 to 64 bytes, isochronous packets
// 1 to 1023 bytes with bInterval 1 (one frame, USB 2.0 section 5.6.4). Isochronous endpoints carry the sync type and the usage
// in bmAttributes; the other types have neither.
constexpr std::array<uint8, 7> MakeUSBEndpointDescriptor(
	uint8 address,
	USBEndpointType type,
	uint16 maxPacketSize,
	uint8 interval,
	USBIsoSync sync = USBIsoSync::None,
	USBIsoUsage usage = USBIsoUsage::Data)
{
	Require<"USB: endpoint 0 belongs to the control pipe, not to a configuration">(USBEndpoint::Number(address) != 0);
	// Bits 6..4 of bEndpointAddress are reserved and must be zero (USB 2.0 section 9.6.6). USBEndpoint::Number() ignores them,
	// so a set bit would make the descriptor and the controller disagree about the endpoint.
	constexpr uint8 reservedBits = 0x70;
	Require<"USB: endpoint address bits 6..4 are reserved">((address & reservedBits) == 0);

	std::array<uint8, 7> bytes{};
	bytes[0] = 7;
	bytes[1] = static_cast<uint8>(USBDescriptorType::Endpoint);
	bytes[2] = address;
	if (type == USBEndpointType::Bulk) {
		Require<"USB: full-speed bulk packets are 8, 16, 32 or 64 bytes">(maxPacketSize == 8 || maxPacketSize == 16 || maxPacketSize == 32 || maxPacketSize == 64);
		Require<"USB: bInterval of a full-speed bulk endpoint is 0">(interval == 0);
		Require<"USB: sync type and usage are isochronous only">(sync == USBIsoSync::None && usage == USBIsoUsage::Data);
		bytes[3] = 0x02;
	} else if (type == USBEndpointType::Isochronous) {
		Require<"USB: full-speed isochronous packets are 1 to 1023 bytes">(maxPacketSize >= 1 && maxPacketSize <= 1023);
		Require<"USB: bInterval of a full-speed isochronous endpoint is 1">(interval == 1);
		bytes[3] = static_cast<uint8>(0x01 | (static_cast<uint8>(sync) << 2) | (static_cast<uint8>(usage) << 4));
	} else {
		Require<"USB: the stack has bulk, interrupt and isochronous endpoints only">(type == USBEndpointType::Interrupt);
		Require<"USB: full-speed interrupt packets are 1 to 64 bytes">(maxPacketSize >= 1 && maxPacketSize <= 64);
		Require<"USB: an interrupt endpoint needs bInterval 1 to 255">(interval >= 1);
		Require<"USB: sync type and usage are isochronous only">(sync == USBIsoSync::None && usage == USBIsoUsage::Data);
		bytes[3] = 0x03;
	}

	USBDescriptorPut16(bytes, 4, maxPacketSize);
	bytes[6] = interval;
	return bytes;
}


// The audio-class form of the endpoint descriptor (USB Audio 1.0 section 4.8.1.1, also used by USB-MIDI 1.0 for its bulk
// endpoints): the standard seven bytes followed by bRefresh and bSynchAddress, 9 bytes in all. The audio class defines this form
// for bulk and isochronous endpoints only, so an interrupt endpoint is refused here. Both fields are zero for a bulk endpoint, which
// has no refresh rate or synchronisation endpoint. An isochronous endpoint may name its feedback endpoint in bSynchAddress (an IN
// address) and its refresh rate in bRefresh; the defaults give an endpoint without feedback.
constexpr std::array<uint8, 9> MakeUSBAudioEndpointDescriptor(
	uint8 address,
	USBEndpointType type,
	uint16 maxPacketSize,
	uint8 interval,
	USBIsoSync sync = USBIsoSync::None,
	USBIsoUsage usage = USBIsoUsage::Data,
	uint8 refresh = 0,
	uint8 synchAddress = 0)
{
	Require<"USB: the 9-byte endpoint form is bulk or isochronous">(type == USBEndpointType::Bulk || type == USBEndpointType::Isochronous);
	Require<"USB: bRefresh and bSynchAddress are 0 for bulk endpoints">(type == USBEndpointType::Isochronous || (refresh == 0 && synchAddress == 0));
	std::array<uint8, 7> standard = MakeUSBEndpointDescriptor(address, type, maxPacketSize, interval, sync, usage);
	std::array<uint8, 9> bytes{};
	for (std::size_t index = 0; index < standard.size(); index++) {
		bytes[index] = standard[index];
	}

	bytes[0] = 9;
	bytes[7] = refresh;
	bytes[8] = synchAddress;
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
	Require<"USB: an association covers at least one interface">(interfaceCount >= 1);
	Require<"USB: an association covers interfaces 0 to 255">(static_cast<uint16>(firstInterface) + interfaceCount <= 256);

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
	Require<"USB: a descriptor is 2 to 255 bytes">(Size >= 2 && Size <= 255);
	Require<"USB: bLength must equal the size of the array">(static_cast<std::size_t>(bytes[0]) == Size);

	uint8 type = bytes[1];
	Require<"USB: configuration descriptors come from MakeUSBConfigurationDescriptor">(type != static_cast<uint8>(USBDescriptorType::Configuration));
	Require<"USB: interface descriptors come from MakeUSBInterfaceDescriptor">(type != static_cast<uint8>(USBDescriptorType::Interface));
	Require<"USB: endpoint descriptors come from MakeUSBEndpointDescriptor">(type != static_cast<uint8>(USBDescriptorType::Endpoint));
	Require<"USB: association descriptors come from MakeUSBInterfaceAssociationDescriptor">(type != static_cast<uint8>(USBDescriptorType::InterfaceAssociation));

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
		Require<"USB: a descriptor header is cut off">(offset + 2 <= Size);
		std::size_t length = bytes[offset];
		Require<"USB: a descriptor is at least 2 bytes">(length >= 2);
		Require<"USB: a descriptor runs past the configuration">(offset + length <= Size);
		uint8 type = bytes[offset + 1];
		Require<"USB: a device-level descriptor cannot be part of a configuration">(!USBIsDeviceLevelType(type));
		if (type == static_cast<uint8>(USBDescriptorType::Interface)) {
			Require<"USB: an interface descriptor is 9 bytes">(length == 9);
			if (inInterface) {
				bytes[currentInterface + 4] = static_cast<uint8>(endpointCount);
			}

			uint8 number = bytes[offset + 2];
			uint8 alternate = bytes[offset + 3];
			if (alternate == 0) {
				Require<"USB: interface numbers are unique">(!interfaceSeen[number]);
				interfaceSeen[number] = true;
				interfaceCount++;
			} else {
				Require<"USB: an alternate setting follows its default setting">(interfaceSeen[number]);
			}

			currentInterface = offset;
			inInterface = true;
			endpointCount = 0;
		} else if (type == static_cast<uint8>(USBDescriptorType::Endpoint)) {
			Require<"USB: an endpoint follows an interface">(inInterface);
			Require<"USB: an endpoint descriptor is 7 bytes, or 9 bytes in the audio form">(length == 7 || length == 9);
			// Bits 6..4 of bEndpointAddress are reserved, as MakeUSBEndpointDescriptor() checks. A plain array gets the same rule here.
			Require<"USB: endpoint address bits 6..4 are reserved">((bytes[offset + 2] & 0x70) == 0);
			uint8 transferType = bytes[offset + 3] & 0x03;
			if (transferType == 0x01) {
				// The isochronous rules of MakeUSBEndpointDescriptor(), for a plain array as well: one frame, and 1 to 1023 bytes.
				uint16 packetSize = static_cast<uint16>(bytes[offset + 4] | (bytes[offset + 5] << 8));
				Require<"USB: bInterval of a full-speed isochronous endpoint is 1">(bytes[offset + 6] == 1);
				Require<"USB: full-speed isochronous packets are 1 to 1023 bytes">(packetSize >= 1 && packetSize <= 1023);
			}

			if (length == 9) {
				// The audio form adds bRefresh and bSynchAddress. The audio class defines it for bulk and isochronous endpoints only, so
				// an interrupt endpoint is refused. A bulk endpoint has both fields zero. An isochronous endpoint names its feedback endpoint
				// in bSynchAddress: 0, or an IN address (bit 7 set, number 1 to 15, bits 6..4 clear).
				Require<"USB: the 9-byte endpoint form is bulk or isochronous">(transferType == 0x01 || transferType == 0x02);
				if (transferType == 0x02) {
					Require<"USB: bRefresh and bSynchAddress are 0 for bulk endpoints">(bytes[offset + 7] == 0 && bytes[offset + 8] == 0);
				} else {
					uint8 synchAddress = bytes[offset + 8];
					bool feedbackAddress = (synchAddress & 0x80) != 0 && (synchAddress & 0x70) == 0 && (synchAddress & 0x0F) != 0;
					Require<"USB: bSynchAddress is 0 or an IN endpoint address">(synchAddress == 0 || feedbackAddress);
				}
			}

			uint8 address = bytes[offset + 2];
			Require<"USB: endpoint addresses are unique in the configuration">(!endpointSeen[address]);
			endpointSeen[address] = true;
			endpointCount++;
		} else if (type == static_cast<uint8>(USBDescriptorType::InterfaceAssociation)) {
			Require<"USB: an interface association descriptor is 8 bytes">(length == 8);
			std::size_t first = bytes[offset + 2];
			std::size_t count = bytes[offset + 3];
			Require<"USB: an association covers one or more interfaces from 0 to 255">(count >= 1 && first + count <= 256);
			for (std::size_t number = first; number < first + count; number++) {
				Require<"USB: an association precedes the interfaces it covers">(!interfaceSeen[number]);
				Require<"USB: an interface belongs to one association">(!interfaceAssociated[number]);
				interfaceAssociated[number] = true;
			}
		}

		offset += length;
	}

	if (inInterface) {
		bytes[currentInterface + 4] = static_cast<uint8>(endpointCount);
	}

	Require<"USB: a configuration has at least one interface">(interfaceCount >= 1);
	Require<"USB: bNumInterfaces is one byte">(interfaceCount <= 255);
	for (std::size_t number = 0; number < 256; number++) {
		if (interfaceAssociated[number]) {
			Require<"USB: an association covers an interface the configuration lacks">(interfaceSeen[number]);
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
	Require<"USB: bConfigurationValue 0 means not configured">(configurationValue >= 1);
	Require<"USB: wTotalLength is 16 bits">(total <= 0xFFFF);
	Require<"USB: bMaxPower is at most 500 mA">(maxPower <= 250);

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
	Require<"USB: a configuration descriptor is at least 9 bytes">(Size >= 9);
	Require<"USB: the bytes are a configuration descriptor">(configuration[1] == static_cast<uint8>(USBDescriptorType::Configuration));
	Require<"USB: wTotalLength must equal the configuration's size">(USBDescriptorRead16(configuration, 2) == Size);
	bool associated = false;
	std::size_t offset = 9;
	while (offset < Size) {
		Require<"USB: a descriptor header is cut off">(offset + 2 <= Size);
		std::size_t length = configuration[offset];
		Require<"USB: a descriptor is at least 2 bytes">(length >= 2);
		Require<"USB: a descriptor runs past the configuration">(offset + length <= Size);
		if (configuration[offset + 1] == static_cast<uint8>(USBDescriptorType::InterfaceAssociation)) {
			associated = true;
		}

		offset += length;
	}

	return associated;
}
