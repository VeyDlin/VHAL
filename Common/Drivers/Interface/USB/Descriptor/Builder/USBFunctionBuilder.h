#pragma once
#include <array>
#include <concepts>
#include <cstddef>
#include <span>
#include <string_view>
#include <Adapter/Utilities/USBTypes.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBBuilderBuffer.h>
#include <Drivers/Interface/USB/Descriptor/USBConfigurationDescriptor.h>
#include <Drivers/Interface/USB/USBProtocol.h>


// Limits of one configuration. They size the fixed arrays of the chain; a chain that exceeds one fails with the message.
constexpr std::size_t USBMaxStrings = 32;
constexpr std::size_t USBMaxFunctions = 8;
constexpr std::size_t USBMaxEndpoints = 32;
constexpr std::size_t USBMaxFunctionEndpoints = 16;
constexpr std::size_t USBMaxFunctionStrings = 4;
constexpr std::size_t USBMaxPatches = 128;


// True when text is a WinUSB interface GUID in registry form: 38 characters, braces, hexadecimal digits and hyphens at 9, 14, 19
// and 24. The DeviceInterfaceGUIDs property carries the text as given, so a malformed one would never match the host's open call.
constexpr bool USBGuidIsValid(std::string_view text) {
	if (text.size() != 38 || text[0] != '{' || text[37] != '}') {
		return false;
	}

	for (std::size_t index = 1; index < 37; index++) {
		char value = text[index];
		bool dash = index == 9 || index == 14 || index == 19 || index == 24;
		bool digit = (value >= '0' && value <= '9') || (value >= 'A' && value <= 'F') || (value >= 'a' && value <= 'f');
		if (dash && value != '-') {
			return false;
		}

		if (!dash && !digit) {
			return false;
		}
	}

	return true;
}


// Passed as the string index of Interface() when the interface has no string.
constexpr std::size_t USBNoString = static_cast<std::size_t>(-1);


// One endpoint a class asks for. A class states what it needs; the configuration builder assigns the address unless the
// application gave one. address 0 means assigned by the builder. maxPacketSize and interval are checked by
// MakeUSBEndpointDescriptor() when the endpoint is written. An isochronous request sets interval 1 and may set the sync type and
// the usage; the other kinds leave both at None and Data.
struct USBEndpointRequest {
	USBEndpointType type = USBEndpointType::Bulk;
	USBDirection direction = USBDirection::In;
	uint8 address = 0;
	uint16 maxPacketSize = 64;
	uint8 interval = 0;
	USBIsoSync isoSync = USBIsoSync::None;
	USBIsoUsage isoUsage = USBIsoUsage::Data;
};


// A string index written into the configuration before the device fixes its string table. The emitter writes the function's
// local index (1-based, into the configuration's own list); the device rewrites it to the global index when it joins the two.
struct USBStringPatch {
	std::size_t offset = 0;
	std::size_t local = 0;
};


struct USBPatchList {
	std::array<USBStringPatch, USBMaxPatches> entries{};
	std::size_t count = 0;


	constexpr void Add(std::size_t offset, std::size_t local) {
		Require<"too many string references in one configuration">(count < USBMaxPatches);
		entries[count] = USBStringPatch{ offset, local };
		count++;
	}
};


// What the configuration builder resolved for one function before the function emits its descriptors: its interface numbers,
// its endpoint addresses in the order the class requested them, and the local indexes of its strings.
struct USBFunctionSlot {
	uint8 firstInterface = 0;
	std::size_t interfaceCount = 0;
	std::array<USBEndpointRequest, USBMaxFunctionEndpoints> requests{};
	std::array<uint8, USBMaxFunctionEndpoints> addresses{};
	std::size_t endpointCount = 0;
	std::array<std::size_t, USBMaxFunctionStrings> strings{};
	std::size_t stringCount = 0;
	// Position of the function's first run-time string in the configuration's list, and how many it reserved.
	std::size_t runtimeStart = 0;
	std::size_t runtimeCount = 0;
};


// The writing side a class uses in Emit(). The class writes its own interface, class-specific and endpoint descriptors in the
// order it needs; the emitter supplies the numbers, addresses and string indexes the configuration resolved.
class USBFunctionEmitter {
private:
	USBBuildBuffer& body;
	USBPatchList& patches;
	USBPatchList& runtimePatches;
	const USBFunctionSlot& slot;
	// The interface whose run of alternate settings is open, and the last alternate setting written for it. Alternate() checks
	// them, so the settings of one interface are listed directly after its alternate 0, in ascending order.
	std::size_t openIndex = USBNoString;
	uint8 lastAlternate = 0;


public:
	constexpr USBFunctionEmitter(
		USBBuildBuffer& bodyBuffer,
		USBPatchList& patchList,
		USBPatchList& runtimePatchList,
		const USBFunctionSlot& functionSlot)
		: body(bodyBuffer),
		  patches(patchList),
		  runtimePatches(runtimePatchList),
		  slot(functionSlot) {
	}


	// Interface number of the function's index-th interface (0 based within the function).
	constexpr uint8 InterfaceNumber(std::size_t index) const {
		Require<"interface index is outside the function">(index < slot.interfaceCount);
		return static_cast<uint8>(slot.firstInterface + index);
	}


	// Address of the index-th endpoint request, as the configuration resolved it.
	constexpr uint8 EndpointAddress(std::size_t index) const {
		Require<"endpoint index is outside the function">(index < slot.endpointCount);
		return slot.addresses[index];
	}


	// Interface descriptor with alternate setting 0. stringIndex is an index into the function's strings, or USBNoString.
	// bNumEndpoints is set by the configuration after the endpoints are written. It opens the run of alternate settings of
	// the interface, so Alternate() may follow it.
	constexpr void Interface(std::size_t index, uint8 interfaceClass, uint8 subClass, uint8 protocol, std::size_t stringIndex) {
		WriteInterface(index, 0, interfaceClass, subClass, protocol, stringIndex);
		openIndex = index;
		lastAlternate = 0;
	}


	// Interface descriptor with alternate setting alternateSetting (USB 2.0 section 9.6.5) of the index-th interface. It shares
	// the interface number of alternate 0, so the configuration counts the interface once and bNumInterfaces does not change.
	// The settings of one interface are listed one after another, directly after its alternate 0, in ascending order, each once:
	// the host reads them in that order, and a setting out of place would not be the one the host selects. Alternate 0 is
	// written by Interface(). stringIndex is as in Interface(), so each setting can name its own memory region.
	constexpr void Alternate(
		std::size_t index,
		uint8 alternateSetting,
		uint8 interfaceClass,
		uint8 subClass,
		uint8 protocol,
		std::size_t stringIndex)
	{
		Require<"alternate 0 is written by Interface()">(alternateSetting != 0);
		Require<"an alternate setting follows its alternate 0 directly">(index == openIndex);
		Require<"an alternate setting is given once">(alternateSetting != lastAlternate);
		Require<"alternate settings are listed in ascending order">(alternateSetting > lastAlternate);
		WriteInterface(index, alternateSetting, interfaceClass, subClass, protocol, stringIndex);
		lastAlternate = alternateSetting;
	}


	// Endpoint descriptor of the index-th endpoint request, with the address the configuration resolved.
	constexpr void Endpoint(std::size_t index) {
		Require<"endpoint index is outside the function">(index < slot.endpointCount);
		const USBEndpointRequest& request = slot.requests[index];
		body.Append(MakeUSBEndpointDescriptor(
			slot.addresses[index],
			request.type,
			request.maxPacketSize,
			request.interval,
			request.isoSync,
			request.isoUsage
		));
	}


	// Endpoint descriptor of the index-th endpoint request in the audio-class form (9 bytes, see MakeUSBAudioEndpointDescriptor()),
	// with the address the configuration resolved. Used by USB-MIDI for its bulk endpoints and by USB Audio for its endpoints, which
	// are isochronous or bulk.
	constexpr void AudioEndpoint(std::size_t index) {
		Require<"endpoint index is outside the function">(index < slot.endpointCount);
		const USBEndpointRequest& request = slot.requests[index];
		body.Append(MakeUSBAudioEndpointDescriptor(
			slot.addresses[index],
			request.type,
			request.maxPacketSize,
			request.interval,
			request.isoSync,
			request.isoUsage
		));
	}


	// A class-specific descriptor given as bytes, such as a functional or HID descriptor. Its bLength must equal its size, and
	// the structural types (configuration, interface, endpoint, association) are not accepted here: the emitter writes them.
	constexpr void Raw(std::span<const uint8> bytes) {
		CheckRaw(bytes);
		body.Append(bytes);
	}


	// A class-specific descriptor with a string index byte at stringOffset, such as the iJack of a USB-MIDI jack. The byte is given
	// as 0 and the configuration fills it in: stringIndex is an index into the function's strings, or USBNoString, which leaves
	// the byte 0.
	constexpr void RawWithString(std::span<const uint8> bytes, std::size_t stringOffset, std::size_t stringIndex) {
		CheckRaw(bytes);
		Require<"the string index byte is inside the descriptor">(stringOffset < bytes.size());
		Require<"the string index byte is given as 0 and filled in by the configuration">(bytes[stringOffset] == 0);
		if (stringIndex != USBNoString) {
			Require<"string index is outside the function">(stringIndex < slot.stringCount);
			patches.Add(body.size + stringOffset, slot.strings[stringIndex]);
		}

		body.Append(bytes);
	}


	// A class-specific descriptor with the index of one of the function's run-time strings at stringOffset, such as iMACAddress of
	// the Ethernet functional descriptor. The byte is given as 0. The index is not known until the device joins the configuration
	// (its fixed strings come first), so the device writes it then. runtimeIndex counts the function's run-time strings from 0, in
	// the order the class reserved them. The class cannot read the index in Emit(); the function gets it from its layout.
	constexpr void RawWithRuntimeString(std::span<const uint8> bytes, std::size_t stringOffset, std::size_t runtimeIndex) {
		CheckRaw(bytes);
		Require<"the run-time string index byte is inside the descriptor">(stringOffset < bytes.size());
		Require<"the run-time string index byte is given as 0 and filled in by the device">(bytes[stringOffset] == 0);
		Require<"run-time string index is outside the function">(runtimeIndex < slot.runtimeCount);
		runtimePatches.Add(body.size + stringOffset, slot.runtimeStart + runtimeIndex);
		body.Append(bytes);
	}


private:
	// Writes one interface descriptor and records the patch of its iInterface byte, the byte 8 of the descriptor. The alternate
	// setting is given as it is; Interface() and Alternate() check the order before they call this.
	constexpr void WriteInterface(
		std::size_t index,
		uint8 alternateSetting,
		uint8 interfaceClass,
		uint8 subClass,
		uint8 protocol,
		std::size_t stringIndex)
	{
		if (stringIndex != USBNoString) {
			Require<"string index is outside the function">(stringIndex < slot.stringCount);
			patches.Add(body.size + 8, slot.strings[stringIndex]);
		}

		body.Append(MakeUSBInterfaceDescriptor(InterfaceNumber(index), alternateSetting, interfaceClass, subClass, protocol, 0));
	}


	// The rules Raw() and RawWithString() share: the size, bLength and the structural types.
	static constexpr void CheckRaw(std::span<const uint8> bytes) {
		Require<"a descriptor is 2 to 255 bytes">(bytes.size() >= 2 && bytes.size() <= 255);
		Require<"bLength must equal the size of the descriptor">(static_cast<std::size_t>(bytes[0]) == bytes.size());
		uint8 type = bytes[1];
		Require<"structural descriptors come from the emitter's Interface() and Endpoint()">(
			type != static_cast<uint8>(USBDescriptorType::Configuration)
			&& type != static_cast<uint8>(USBDescriptorType::Interface)
			&& type != static_cast<uint8>(USBDescriptorType::Endpoint)
			&& type != static_cast<uint8>(USBDescriptorType::InterfaceAssociation));
	}
};


// The compile-time concept a class builder implements to plug into USBConfigurationBuilder::Function(). The configuration
// builder calls these members in this order:
//   InterfaceCount()      number of interfaces the class uses (1 or more)
//   EndpointCount(), Endpoint(i)   endpoints it needs, in the order its descriptors and its driver expect them
//   StringCount(), String(i)       strings it names (interface names); the configuration numbers them in order of first use
//   FunctionClass(), FunctionSubClass(), FunctionProtocol()   the interface association when the function has two or more
//   HasWinUsb(), WinUsbGuid()      an optional WinUSB binding; an empty GUID gives the compatible ID only
//   Emit(emitter)         the interface, class-specific and endpoint descriptors, once numbers, addresses and strings are known
//
// Optional, not part of the concept: RuntimeStringCount(), the number of run-time strings the class reserves (see
// USBDevice::SetRuntimeString()). A class without it reserves none. The class writes each string's index with
// emitter.RawWithRuntimeString(). Every reserved string must be written, or the configuration is refused.
template<typename T>
concept USBClassFunctionBuilder = requires(const T& function, USBFunctionEmitter& emitter) {
	{ function.InterfaceCount() } -> std::convertible_to<std::size_t>;
	{ function.EndpointCount() } -> std::convertible_to<std::size_t>;
	{ function.Endpoint(std::size_t{}) } -> std::same_as<USBEndpointRequest>;
	{ function.StringCount() } -> std::convertible_to<std::size_t>;
	{ function.String(std::size_t{}) } -> std::convertible_to<std::string_view>;
	{ function.FunctionClass() } -> std::convertible_to<uint8>;
	{ function.FunctionSubClass() } -> std::convertible_to<uint8>;
	{ function.FunctionProtocol() } -> std::convertible_to<uint8>;
	{ function.HasWinUsb() } -> std::convertible_to<bool>;
	{ function.WinUsbGuid() } -> std::convertible_to<std::string_view>;
	{ function.Emit(emitter) } -> std::same_as<void>;
};


// The run-time strings a class reserves through its optional RuntimeStringCount(). A class without the member reserves none.
template<typename T>
constexpr std::size_t USBRuntimeStringCount(const T& function) {
	if constexpr (requires(const T& value) {
		{ value.RuntimeStringCount() } -> std::convertible_to<std::size_t>;
	}) {
		return function.RuntimeStringCount();
	} else {
		static_cast<void>(function);
		return 0;
	}
}
