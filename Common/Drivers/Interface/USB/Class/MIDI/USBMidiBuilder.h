#pragma once
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <Adapter/Utilities/USBTypes.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBBuilderBuffer.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBFunctionBuilder.h>
#include <Drivers/Interface/USB/Descriptor/USBConfigurationDescriptor.h>
#include <Drivers/Interface/USB/USBProtocol.h>


// Constants of the Audio Device Class 1.0 (the Audio Control interface, section 4.3) and of the MIDI Device Class 1.0 (the
// MIDIStreaming interface and its jacks, section 6). The core's USBClassCode does not list audio, so they are kept here.
constexpr uint8 USBMidiAudioClassCode = 0x01;
constexpr uint8 USBMidiAudioControlSubClass = 0x01;
constexpr uint8 USBMidiStreamingSubClass = 0x03;
constexpr uint8 USBMidiClassSpecificInterface = 0x24;
constexpr uint8 USBMidiClassSpecificEndpoint = 0x25;
constexpr uint8 USBMidiAcHeaderSubtype = 0x01;
constexpr uint8 USBMidiMsHeaderSubtype = 0x01;
constexpr uint8 USBMidiInJackSubtype = 0x02;
constexpr uint8 USBMidiOutJackSubtype = 0x03;
constexpr uint8 USBMidiMsGeneralSubtype = 0x01;
constexpr uint8 USBMidiJackEmbedded = 0x01;
constexpr uint8 USBMidiJackExternal = 0x02;
constexpr uint8 USBMidiMaxCables = 16;


// The four jacks of one cable, in the order their IDs are numbered. The embedded IN jack receives the packets the host sends on
// the bulk OUT endpoint; the embedded OUT jack feeds the bulk IN endpoint. The external jacks connect to the MIDI wires.
enum class USBMidiJack : uint8 {
	EmbeddedIn,
	ExternalIn,
	EmbeddedOut,
	ExternalOut
};


// Jack ID of one jack of a cable. The cables number their jacks in blocks of four from 1, so cable 0 has 1 to 4 and cable 1 has
// 5 to 8. The IDs are unique in the function, and the descriptors refer to them by these values.
constexpr uint8 USBMidiJackId(uint8 cable, USBMidiJack jack) {
	return static_cast<uint8>(cable * 4 + static_cast<uint8>(jack) + 1);
}


// The class-specific Audio Control header (Audio 1.0 section 4.3.2) for one MIDIStreaming interface: bcdADC 0x0100, wTotalLength
// 9 (the header and one interface number), bInCollection 1 and baInterfaceNr the streaming interface.
constexpr std::array<uint8, 9> MakeUSBMidiAudioControlHeader(uint8 streamingInterface) {
	std::array<uint8, 9> bytes{};
	bytes[0] = 9;
	bytes[1] = USBMidiClassSpecificInterface;
	bytes[2] = USBMidiAcHeaderSubtype;
	USBDescriptorPut16(bytes, 3, 0x0100);
	USBDescriptorPut16(bytes, 5, 9);
	bytes[7] = 1;
	bytes[8] = streamingInterface;
	return bytes;
}


// The class-specific MS interface header (MIDI 1.0 section 6.1.2.1): bcdMSC 0x0100 and the total length of the class-specific
// descriptors of the interface, this header included. The endpoint descriptors are not part of it.
constexpr std::array<uint8, 7> MakeUSBMidiStreamingHeader(uint16 totalLength) {
	std::array<uint8, 7> bytes{};
	bytes[0] = 7;
	bytes[1] = USBMidiClassSpecificInterface;
	bytes[2] = USBMidiMsHeaderSubtype;
	USBDescriptorPut16(bytes, 3, 0x0100);
	USBDescriptorPut16(bytes, 5, totalLength);
	return bytes;
}


// MIDI IN jack (section 6.1.2.2). An IN jack has no source. The last byte is iJack, which the configuration fills in.
constexpr std::array<uint8, 6> MakeUSBMidiInJack(uint8 jackType, uint8 jackId) {
	std::array<uint8, 6> bytes{};
	bytes[0] = 6;
	bytes[1] = USBMidiClassSpecificInterface;
	bytes[2] = USBMidiInJackSubtype;
	bytes[3] = jackType;
	bytes[4] = jackId;
	return bytes;
}


// MIDI OUT jack with one input pin (section 6.1.2.3). The jack takes its data from output pin 1 of the source jack, which is the
// only output pin an IN jack has. The last byte is iJack, which the configuration fills in.
constexpr std::array<uint8, 9> MakeUSBMidiOutJack(uint8 jackType, uint8 jackId, uint8 sourceId) {
	std::array<uint8, 9> bytes{};
	bytes[0] = 9;
	bytes[1] = USBMidiClassSpecificInterface;
	bytes[2] = USBMidiOutJackSubtype;
	bytes[3] = jackType;
	bytes[4] = jackId;
	bytes[5] = 1;
	bytes[6] = sourceId;
	bytes[7] = 1;
	return bytes;
}


// The class-specific MS bulk data endpoint descriptor (section 6.2.2) for a bulk endpoint: bNumEmbMIDIJack and the IDs of the
// embedded jacks it carries. The array has room for the largest cable count; the descriptor is its first 4 + count bytes.
constexpr std::array<uint8, 4 + USBMidiMaxCables> MakeUSBMidiEndpointJacks(const std::array<uint8, USBMidiMaxCables>& jackIds, uint8 count) {
	USBDescriptorRequire(count >= 1 && count <= USBMidiMaxCables, "a MIDI endpoint lists 1 to 16 embedded jacks");
	std::array<uint8, 4 + USBMidiMaxCables> bytes{};
	bytes[0] = static_cast<uint8>(4 + count);
	bytes[1] = USBMidiClassSpecificEndpoint;
	bytes[2] = USBMidiMsGeneralSubtype;
	bytes[3] = count;
	for (std::size_t index = 0; index < count; index++) {
		bytes[4 + index] = jackIds[index];
	}

	return bytes;
}


// Class builder of a USB-MIDI 1.0 function (USBConfigurationBuilder::Function()). It has two interfaces: an Audio Control interface
// with no endpoints, and the MIDIStreaming interface with a bulk OUT and a bulk IN endpoint of 64 bytes. The configuration writes
// the interface association over both. Each cable has four jacks: an embedded and an external IN jack, and an embedded and an external
// OUT jack. The external OUT jack sends what the embedded IN jack receives, and the embedded OUT jack carries what the external IN
// jack receives.
//
// The layout lists the Audio Control interface first, then the MIDIStreaming interface. Its endpoints are the bulk OUT endpoint
// first and the bulk IN endpoint second, the order of the MIDI 1.0 descriptors. MidiFunction accepts either order.
//
// Names are optional. Name() is the MIDIStreaming interface string. JackName() is the iJack of every jack; the jacks of all cables
// share one string, since the function holds a few strings only.
class USBMidiBuilder {
private:
	uint8 cableCount = 0;
	uint8 bulkOutAddress = 0;
	uint8 bulkInAddress = 0;
	std::string_view name;
	std::string_view jackName;


public:
	// Number of cables, 1 to 16. Required: the jacks and the endpoint descriptors are written for each cable.
	constexpr USBMidiBuilder& Cables(uint8 count) {
		Require<USBText{"a USB-MIDI function has 1 to 16 cables"}>(count >= 1 && count <= USBMidiMaxCables);
		cableCount = count;
		return *this;
	}


	// Address of the bulk OUT endpoint, the host to device direction. Without it the configuration builder assigns the number.
	constexpr USBMidiBuilder& BulkOut(uint8 address) {
		Require<USBText{"BulkOut needs an OUT address (bit 7 clear)"}>(!USBEndpoint::IsIn(address));
		Require<USBText{"endpoint 0 belongs to the control pipe"}>(USBEndpoint::Number(address) != 0);
		bulkOutAddress = address;
		return *this;
	}


	// Address of the bulk IN endpoint, the device to host direction. Without it the configuration builder assigns the number.
	constexpr USBMidiBuilder& BulkIn(uint8 address) {
		Require<USBText{"BulkIn needs an IN address (bit 7 set)"}>(USBEndpoint::IsIn(address));
		Require<USBText{"endpoint 0 belongs to the control pipe"}>(USBEndpoint::Number(address) != 0);
		bulkInAddress = address;
		return *this;
	}


	// The MIDIStreaming interface string.
	constexpr USBMidiBuilder& Name(std::string_view text) {
		name = text;
		return *this;
	}


	// The iJack string of every jack.
	constexpr USBMidiBuilder& JackName(std::string_view text) {
		jackName = text;
		return *this;
	}


	constexpr std::size_t InterfaceCount() const {
		Require<USBText{"a USB-MIDI interface needs its cables: call Cables()"}>(cableCount >= 1);
		return 2;
	}


	constexpr std::size_t EndpointCount() const {
		return 2;
	}


	constexpr USBEndpointRequest Endpoint(std::size_t index) const {
		USBEndpointRequest request;
		request.type = USBEndpointType::Bulk;
		request.direction = index == 0 ? USBDirection::Out : USBDirection::In;
		request.address = index == 0 ? bulkOutAddress : bulkInAddress;
		request.maxPacketSize = 64;
		request.interval = 0;
		return request;
	}


	constexpr std::size_t StringCount() const {
		return (name.empty() ? 0 : 1) + (jackName.empty() ? 0 : 1);
	}


	constexpr std::string_view String(std::size_t index) const {
		return index == NameIndex() ? name : jackName;
	}


	constexpr uint8 FunctionClass() const {
		return USBMidiAudioClassCode;
	}


	constexpr uint8 FunctionSubClass() const {
		return 0x00;
	}


	constexpr uint8 FunctionProtocol() const {
		return 0x00;
	}


	constexpr bool HasWinUsb() const {
		return false;
	}


	constexpr std::string_view WinUsbGuid() const {
		return std::string_view();
	}


	constexpr void Emit(USBFunctionEmitter& emitter) const {
		emitter.Interface(0, USBMidiAudioClassCode, USBMidiAudioControlSubClass, 0x00, USBNoString);
		std::array<uint8, 9> audioControl = MakeUSBMidiAudioControlHeader(emitter.InterfaceNumber(1));
		emitter.Raw(audioControl);

		emitter.Interface(1, USBMidiAudioClassCode, USBMidiStreamingSubClass, 0x00, NameIndex());
		// wTotalLength counts the MS header and the four jacks of every cable. The endpoint descriptors are not in it.
		std::array<uint8, 7> streaming = MakeUSBMidiStreamingHeader(static_cast<uint16>(7 + cableCount * (6 + 6 + 9 + 9)));
		emitter.Raw(streaming);

		std::array<uint8, USBMidiMaxCables> embeddedIns{};
		std::array<uint8, USBMidiMaxCables> embeddedOuts{};
		for (uint8 cable = 0; cable < cableCount; cable++) {
			uint8 embeddedIn = USBMidiJackId(cable, USBMidiJack::EmbeddedIn);
			uint8 externalIn = USBMidiJackId(cable, USBMidiJack::ExternalIn);
			uint8 embeddedOut = USBMidiJackId(cable, USBMidiJack::EmbeddedOut);
			uint8 externalOut = USBMidiJackId(cable, USBMidiJack::ExternalOut);

			std::array<uint8, 6> inEmbedded = MakeUSBMidiInJack(USBMidiJackEmbedded, embeddedIn);
			emitter.RawWithString(inEmbedded, 5, JackNameIndex());
			std::array<uint8, 6> inExternal = MakeUSBMidiInJack(USBMidiJackExternal, externalIn);
			emitter.RawWithString(inExternal, 5, JackNameIndex());
			std::array<uint8, 9> outEmbedded = MakeUSBMidiOutJack(USBMidiJackEmbedded, embeddedOut, externalIn);
			emitter.RawWithString(outEmbedded, 8, JackNameIndex());
			std::array<uint8, 9> outExternal = MakeUSBMidiOutJack(USBMidiJackExternal, externalOut, embeddedIn);
			emitter.RawWithString(outExternal, 8, JackNameIndex());

			embeddedIns[cable] = embeddedIn;
			embeddedOuts[cable] = embeddedOut;
		}

		// The OUT endpoint carries packets into the embedded IN jacks, and the IN endpoint carries packets out of the embedded OUT jacks.
		emitter.AudioEndpoint(0);
		std::array<uint8, 4 + USBMidiMaxCables> outJacks = MakeUSBMidiEndpointJacks(embeddedIns, cableCount);
		emitter.Raw(std::span<const uint8>(outJacks).first(static_cast<std::size_t>(4 + cableCount)));
		emitter.AudioEndpoint(1);
		std::array<uint8, 4 + USBMidiMaxCables> inJacks = MakeUSBMidiEndpointJacks(embeddedOuts, cableCount);
		emitter.Raw(std::span<const uint8>(inJacks).first(static_cast<std::size_t>(4 + cableCount)));
	}


private:
	// Index of the interface string among the function's strings, or USBNoString when the interface has none.
	constexpr std::size_t NameIndex() const {
		return name.empty() ? USBNoString : 0;
	}


	// Index of the jack string among the function's strings: after the interface string when both are given.
	constexpr std::size_t JackNameIndex() const {
		if (jackName.empty()) {
			return USBNoString;
		}

		return name.empty() ? 0 : 1;
	}
};
