#pragma once
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <Adapter/Utilities/USBTypes.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBBuilderBuffer.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBFunctionBuilder.h>
#include <Drivers/Interface/USB/Descriptor/USBConfigurationDescriptor.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorUtility.h>
#include <Drivers/Interface/USB/USBProtocol.h>


// Constants of the Audio Device Class 1.0 (the Audio Control and AudioStreaming interfaces, their class-specific descriptors and
// requests) and of the Audio Data Formats 1.0 (PCM, format type I). The core's USBClassCode does not list audio, so they are kept
// here, as the MIDI builder keeps its own.
constexpr uint8 USBAudioClassCode = 0x01;
constexpr uint8 USBAudioControlSubClass = 0x01;
constexpr uint8 USBAudioStreamingSubClass = 0x02;
constexpr uint8 USBAudioClassSpecificInterface = 0x24;
constexpr uint8 USBAudioClassSpecificEndpoint = 0x25;
constexpr uint8 USBAudioHeaderSubtype = 0x01;
constexpr uint8 USBAudioInputTerminalSubtype = 0x02;
constexpr uint8 USBAudioOutputTerminalSubtype = 0x03;
constexpr uint8 USBAudioFeatureUnitSubtype = 0x06;
constexpr uint8 USBAudioGeneralSubtype = 0x01;
constexpr uint8 USBAudioFormatTypeSubtype = 0x02;
constexpr uint8 USBAudioFormatTypeOne = 0x01;
constexpr uint8 USBAudioEndpointGeneralSubtype = 0x01;
constexpr uint16 USBAudioPcmFormat = 0x0001;
constexpr uint16 USBAudioStreamingTerminal = 0x0101;
constexpr uint16 USBAudioMicrophoneTerminal = 0x0201;
constexpr uint16 USBAudioSpeakerTerminal = 0x0301;
// The terminal and unit IDs of the two paths. They are fixed, so the function and a host tool read the same numbers. A path that is
// not given leaves its IDs unused, which the specification allows.
constexpr uint8 USBAudioMicrophoneInput = 1;
constexpr uint8 USBAudioMicrophoneFeature = 2;
constexpr uint8 USBAudioMicrophoneOutput = 3;
constexpr uint8 USBAudioSpeakerInput = 4;
constexpr uint8 USBAudioSpeakerFeature = 5;
constexpr uint8 USBAudioSpeakerOutput = 6;
// Feature unit controls. The control selectors go in the high byte of wValue; the master channel is channel 0. The bits of
// bmaControls of the master channel are the same controls: bit 0 mute, bit 1 volume.
constexpr uint8 USBAudioMuteSelector = 0x01;
constexpr uint8 USBAudioVolumeSelector = 0x02;
constexpr uint8 USBAudioMuteBit = 0x01;
constexpr uint8 USBAudioVolumeBit = 0x02;
// The sampling frequency control of an isochronous endpoint: wValue 0x0100, wIndex the endpoint address.
constexpr uint8 USBAudioSamplingFrequencySelector = 0x01;
// Requests of the audio class (bRequest).
constexpr uint8 USBAudioSetCur = 0x01;
constexpr uint8 USBAudioGetCur = 0x81;
constexpr uint8 USBAudioGetMin = 0x82;
constexpr uint8 USBAudioGetMax = 0x83;
constexpr uint8 USBAudioGetRes = 0x84;
constexpr std::size_t USBAudioMaxRates = 3;
constexpr uint32 USBAudioDefaultRate = 48000;


// Largest packet of a path, in bytes: one sample frame more than the rate's frames per millisecond, so an adaptive sink takes a host
// that sends one extra frame. Each frame holds one 16-bit sample per channel. 48 kHz stereo is 49 * 2 * 2 = 196 bytes.
constexpr uint16 USBAudioPacketBytes(uint32 rate, uint8 channels) {
	return static_cast<uint16>((rate / 1000 + 1) * channels * 2);
}


// Input terminal (Audio Device Class 1.0): 12 bytes. The channel config is 0 for mono and 0x0003 (left and right front) for stereo.
constexpr std::array<uint8, 12> MakeUSBAudioInputTerminal(uint8 terminalId, uint16 terminalType, uint8 channels, uint16 channelConfig) {
	std::array<uint8, 12> bytes{};
	bytes[0] = 12;
	bytes[1] = USBAudioClassSpecificInterface;
	bytes[2] = USBAudioInputTerminalSubtype;
	bytes[3] = terminalId;
	USBDescriptorPut16(bytes, 4, terminalType);
	bytes[6] = 0;
	bytes[7] = channels;
	USBDescriptorPut16(bytes, 8, channelConfig);
	bytes[10] = 0;
	bytes[11] = 0;
	return bytes;
}


// Output terminal: 9 bytes. The string index is 0, since the terminals have no names.
constexpr std::array<uint8, 9> MakeUSBAudioOutputTerminal(uint8 terminalId, uint16 terminalType, uint8 sourceId) {
	std::array<uint8, 9> bytes{};
	bytes[0] = 9;
	bytes[1] = USBAudioClassSpecificInterface;
	bytes[2] = USBAudioOutputTerminalSubtype;
	bytes[3] = terminalId;
	USBDescriptorPut16(bytes, 4, terminalType);
	bytes[6] = 0;
	bytes[7] = sourceId;
	bytes[8] = 0;
	return bytes;
}


// Feature unit with one control byte per channel. The master channel (index 0 of bmaControls) carries masterControls; the channels
// carry none. The unit is 8 + channels bytes long, so the array holds the longest one and the caller takes bLength bytes.
constexpr std::array<uint8, 10> MakeUSBAudioFeatureUnit(uint8 unitId, uint8 sourceId, uint8 channels, uint8 masterControls) {
	std::array<uint8, 10> bytes{};
	uint8 length = static_cast<uint8>(8 + channels);
	bytes[0] = length;
	bytes[1] = USBAudioClassSpecificInterface;
	bytes[2] = USBAudioFeatureUnitSubtype;
	bytes[3] = unitId;
	bytes[4] = sourceId;
	bytes[5] = 1;
	bytes[6] = masterControls;
	bytes[length - 1] = 0;
	return bytes;
}


// AS_GENERAL (7 bytes): the terminal the stream connects to, no delay, and the PCM format.
constexpr std::array<uint8, 7> MakeUSBAudioGeneral(uint8 terminalLink) {
	std::array<uint8, 7> bytes{};
	bytes[0] = 7;
	bytes[1] = USBAudioClassSpecificInterface;
	bytes[2] = USBAudioGeneralSubtype;
	bytes[3] = terminalLink;
	bytes[4] = 0;
	USBDescriptorPut16(bytes, 5, USBAudioPcmFormat);
	return bytes;
}


// FORMAT_TYPE I (Audio Data Formats 1.0): 16-bit PCM in 2-byte subframes, with the discrete sampling frequencies of the stream. Each
// frequency takes 3 bytes, little endian. The array holds three of them; the caller takes bLength bytes.
constexpr std::array<uint8, 17> MakeUSBAudioFormatType(uint8 channels, const std::array<uint32, USBAudioMaxRates>& rates, uint8 rateCount) {
	std::array<uint8, 17> bytes{};
	bytes[0] = static_cast<uint8>(8 + 3 * rateCount);
	bytes[1] = USBAudioClassSpecificInterface;
	bytes[2] = USBAudioFormatTypeSubtype;
	bytes[3] = USBAudioFormatTypeOne;
	bytes[4] = channels;
	bytes[5] = 2;
	bytes[6] = 16;
	bytes[7] = rateCount;
	for (std::size_t index = 0; index < rateCount; index++) {
		std::size_t offset = 8 + 3 * index;
		bytes[offset] = static_cast<uint8>(rates[index] & 0xFF);
		bytes[offset + 1] = static_cast<uint8>((rates[index] >> 8) & 0xFF);
		bytes[offset + 2] = static_cast<uint8>((rates[index] >> 16) & 0xFF);
	}

	return bytes;
}


// The class-specific isochronous endpoint descriptor (7 bytes). Bit 0 of bmAttributes is set when the endpoint has sampling frequency
// control, which is the case when the stream lists more than one rate. The lock delay is 0, which the specification reads as undefined.
constexpr std::array<uint8, 7> MakeUSBAudioEndpointGeneral(bool samplingFrequencyControl) {
	std::array<uint8, 7> bytes{};
	bytes[0] = 7;
	bytes[1] = USBAudioClassSpecificEndpoint;
	bytes[2] = USBAudioEndpointGeneralSubtype;
	bytes[3] = samplingFrequencyControl ? 0x01 : 0x00;
	bytes[4] = 0;
	USBDescriptorPut16(bytes, 5, 0);
	return bytes;
}


// Class builder of a USB Audio 1.0 function (USBConfigurationBuilder::Function()). It has one Audio Control interface and one
// AudioStreaming interface per path: a microphone (device to host, isochronous IN, synchronous) and a speaker (host to device,
// isochronous OUT, adaptive). Each path is an input terminal, a feature unit and an output terminal. The microphone path is
// microphone -> feature unit -> USB streaming; the speaker path is USB streaming -> feature unit -> speaker.
//
// Layout of the interfaces: the Audio Control interface is 0, the microphone's AudioStreaming interface 1 when given, and the
// speaker's the next number. The endpoints are the microphone's first, then the speaker's. Each AudioStreaming interface has alternate
// setting 0 with no endpoints and alternate setting 1 with the endpoint of the path.
//
// The sampling rates of a path are the ones MicrophoneRate() or SpeakerRate() lists. A path with none has 48 kHz. The rate at start
// is the first one listed. With more than one rate the endpoint has sampling frequency control, which the function serves.
//
// Each path has a master-channel mute and volume when MicrophoneMute(), MicrophoneVolume() (and the speaker's) are given. The
// function serves them. Names are optional: Name() is the Audio Control interface string, and the path names are the
// AudioStreaming interface strings.
class USBAudioBuilder {
private:
	// The settings of one path. A path with no channels is absent. The rates are kept as given; rateCount 0 means the default rate.
	struct PathSettings {
		uint8 channels = 0;
		uint8 rateCount = 0;
		std::array<uint32, USBAudioMaxRates> rates{};
		bool volume = false;
		bool mute = false;
		uint8 address = 0;
		std::string_view name;
	};


	PathSettings microphone;
	PathSettings speaker;
	std::string_view name;


public:
	// The microphone path with 1 or 2 channels. Given once. Required for a microphone.
	constexpr USBAudioBuilder& Microphone(uint8 channels) {
		Require<USBText{"the microphone is given once"}>(microphone.channels == 0);
		Require<USBText{"a path has 1 or 2 channels"}>(channels == 1 || channels == 2);
		microphone.channels = channels;
		return *this;
	}


	// A sampling rate of the microphone: 16000, 32000 or 48000, each once. Without any the microphone runs at 48 kHz.
	constexpr USBAudioBuilder& MicrophoneRate(uint32 rate) {
		Require<USBText{"MicrophoneRate needs Microphone() first"}>(microphone.channels != 0);
		AddRate(microphone, rate);
		return *this;
	}


	// Volume control (master channel, -60 dB to 0 dB) of the microphone's feature unit.
	constexpr USBAudioBuilder& MicrophoneVolume() {
		Require<USBText{"MicrophoneVolume needs Microphone() first"}>(microphone.channels != 0);
		microphone.volume = true;
		return *this;
	}


	// Mute control (master channel) of the microphone's feature unit.
	constexpr USBAudioBuilder& MicrophoneMute() {
		Require<USBText{"MicrophoneMute needs Microphone() first"}>(microphone.channels != 0);
		microphone.mute = true;
		return *this;
	}


	// Address of the microphone's isochronous IN endpoint. Without it the configuration builder assigns the number.
	constexpr USBAudioBuilder& MicrophoneAddress(uint8 address) {
		Require<USBText{"MicrophoneAddress needs Microphone() first"}>(microphone.channels != 0);
		Require<USBText{"MicrophoneAddress needs an IN address (bit 7 set)"}>(USBEndpoint::IsIn(address));
		Require<USBText{"endpoint 0 belongs to the control pipe"}>(USBEndpoint::Number(address) != 0);
		microphone.address = address;
		return *this;
	}


	// The microphone's AudioStreaming interface string.
	constexpr USBAudioBuilder& MicrophoneName(std::string_view text) {
		Require<USBText{"MicrophoneName needs Microphone() first"}>(microphone.channels != 0);
		microphone.name = text;
		return *this;
	}


	// The speaker path with 1 or 2 channels. Given once. Required for a speaker.
	constexpr USBAudioBuilder& Speaker(uint8 channels) {
		Require<USBText{"the speaker is given once"}>(speaker.channels == 0);
		Require<USBText{"a path has 1 or 2 channels"}>(channels == 1 || channels == 2);
		speaker.channels = channels;
		return *this;
	}


	// A sampling rate of the speaker: 16000, 32000 or 48000, each once. Without any the speaker runs at 48 kHz.
	constexpr USBAudioBuilder& SpeakerRate(uint32 rate) {
		Require<USBText{"SpeakerRate needs Speaker() first"}>(speaker.channels != 0);
		AddRate(speaker, rate);
		return *this;
	}


	// Volume control (master channel, -60 dB to 0 dB) of the speaker's feature unit.
	constexpr USBAudioBuilder& SpeakerVolume() {
		Require<USBText{"SpeakerVolume needs Speaker() first"}>(speaker.channels != 0);
		speaker.volume = true;
		return *this;
	}


	// Mute control (master channel) of the speaker's feature unit.
	constexpr USBAudioBuilder& SpeakerMute() {
		Require<USBText{"SpeakerMute needs Speaker() first"}>(speaker.channels != 0);
		speaker.mute = true;
		return *this;
	}


	// Address of the speaker's isochronous OUT endpoint. Without it the configuration builder assigns the number.
	constexpr USBAudioBuilder& SpeakerAddress(uint8 address) {
		Require<USBText{"SpeakerAddress needs Speaker() first"}>(speaker.channels != 0);
		Require<USBText{"SpeakerAddress needs an OUT address (bit 7 clear)"}>(!USBEndpoint::IsIn(address));
		Require<USBText{"endpoint 0 belongs to the control pipe"}>(USBEndpoint::Number(address) != 0);
		speaker.address = address;
		return *this;
	}


	// The speaker's AudioStreaming interface string.
	constexpr USBAudioBuilder& SpeakerName(std::string_view text) {
		Require<USBText{"SpeakerName needs Speaker() first"}>(speaker.channels != 0);
		speaker.name = text;
		return *this;
	}


	// The Audio Control interface string. Optional.
	constexpr USBAudioBuilder& Name(std::string_view text) {
		name = text;
		return *this;
	}


	constexpr std::size_t InterfaceCount() const {
		Require<USBText{"an audio function has a microphone or a speaker"}>(HasMicrophone() || HasSpeaker());
		return 1 + StreamingCount();
	}


	constexpr std::size_t EndpointCount() const {
		return StreamingCount();
	}


	constexpr USBEndpointRequest Endpoint(std::size_t index) const {
		Require<USBText{"endpoint index is outside the function"}>(index < EndpointCount());
		bool microphoneEndpoint = HasMicrophone() && index == 0;
		const PathSettings& path = microphoneEndpoint ? microphone : speaker;
		USBEndpointRequest request;
		request.type = USBEndpointType::Isochronous;
		request.direction = microphoneEndpoint ? USBDirection::In : USBDirection::Out;
		request.address = path.address;
		request.maxPacketSize = USBAudioPacketBytes(MaxRate(path), path.channels);
		request.interval = 1;
		// The microphone sends rate / 1000 frames per packet, so its data rate is locked to the host's frames: it is synchronous.
		request.isoSync = microphoneEndpoint ? USBIsoSync::Synchronous : USBIsoSync::Adaptive;
		request.isoUsage = USBIsoUsage::Data;
		return request;
	}


	constexpr std::size_t StringCount() const {
		return (name.empty() ? 0 : 1) + (microphone.name.empty() ? 0 : 1) + (speaker.name.empty() ? 0 : 1);
	}


	constexpr std::string_view String(std::size_t index) const {
		std::size_t position = 0;
		if (!name.empty()) {
			if (index == position) {
				return name;
			}

			position++;
		}

		if (!microphone.name.empty()) {
			if (index == position) {
				return microphone.name;
			}

			position++;
		}

		return speaker.name;
	}


	constexpr uint8 FunctionClass() const {
		return USBAudioClassCode;
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
		emitter.Interface(0, USBAudioClassCode, USBAudioControlSubClass, 0x00, AcNameIndex());
		EmitControl(emitter);

		std::size_t endpointIndex = 0;
		if (HasMicrophone()) {
			EmitStreaming(emitter, 1, endpointIndex, microphone, USBAudioMicrophoneOutput, MicrophoneNameIndex());
			endpointIndex++;
		}

		if (HasSpeaker()) {
			EmitStreaming(emitter, SpeakerIndex(), endpointIndex, speaker, USBAudioSpeakerInput, SpeakerNameIndex());
		}
	}


private:
	constexpr bool HasMicrophone() const {
		return microphone.channels != 0;
	}


	constexpr bool HasSpeaker() const {
		return speaker.channels != 0;
	}


	constexpr std::size_t StreamingCount() const {
		return (HasMicrophone() ? 1 : 0) + (HasSpeaker() ? 1 : 0);
	}


	// Interface index of the speaker's AudioStreaming interface: after the microphone's, when there is one.
	constexpr std::size_t SpeakerIndex() const {
		return HasMicrophone() ? 2 : 1;
	}


	// Number of rates the path lists; one (the default) when none is listed.
	static constexpr uint8 RateCount(const PathSettings& path) {
		return path.rateCount == 0 ? 1 : path.rateCount;
	}


	static constexpr std::array<uint32, USBAudioMaxRates> RatesOf(const PathSettings& path) {
		if (path.rateCount == 0) {
			return std::array<uint32, USBAudioMaxRates>{ USBAudioDefaultRate, 0, 0 };
		}

		return path.rates;
	}


	// The highest rate sets the packet size, so the endpoint takes the largest packet of any rate the path lists.
	static constexpr uint32 MaxRate(const PathSettings& path) {
		std::array<uint32, USBAudioMaxRates> rates = RatesOf(path);
		uint32 highest = 0;
		for (std::size_t index = 0; index < RateCount(path); index++) {
			highest = rates[index] > highest ? rates[index] : highest;
		}

		return highest;
	}


	// Adds a rate to a path. Only the three rates of the specification are accepted, each once.
	static constexpr void AddRate(PathSettings& path, uint32 rate) {
		Require<USBText{"a sample rate is 16000, 32000 or 48000"}>(rate == 16000 || rate == 32000 || rate == 48000);
		for (std::size_t index = 0; index < path.rateCount; index++) {
			Require<USBText{"a sample rate is listed once"}>(path.rates[index] != rate);
		}

		Require<USBText{"a path lists at most 3 sample rates"}>(path.rateCount < USBAudioMaxRates);
		path.rates[path.rateCount] = rate;
		path.rateCount++;
	}


	// Bytes of one path's terminals and feature unit: the input terminal, the feature unit and the output terminal.
	static constexpr std::size_t UnitBytes(const PathSettings& path) {
		return 12 + (8 + path.channels) + 9;
	}


	// bmaControls of the master channel for the controls the path has.
	static constexpr uint8 MasterControls(const PathSettings& path) {
		uint8 controls = 0;
		if (path.mute) {
			controls = static_cast<uint8>(controls | USBAudioMuteBit);
		}

		if (path.volume) {
			controls = static_cast<uint8>(controls | USBAudioVolumeBit);
		}

		return controls;
	}


	static constexpr uint16 ChannelConfig(uint8 channels) {
		return channels == 2 ? 0x0003 : 0x0000;
	}


	// Index of the Audio Control interface string among the function's strings, or USBNoString.
	constexpr std::size_t AcNameIndex() const {
		return name.empty() ? USBNoString : 0;
	}


	constexpr std::size_t MicrophoneNameIndex() const {
		if (microphone.name.empty()) {
			return USBNoString;
		}

		return name.empty() ? 0 : 1;
	}


	constexpr std::size_t SpeakerNameIndex() const {
		if (speaker.name.empty()) {
			return USBNoString;
		}

		return (name.empty() ? 0 : 1) + (microphone.name.empty() ? 0 : 1);
	}


	// The Audio Control header (8 + n bytes), then the terminals and feature units of each path. wTotalLength covers the header and
	// those units.
	constexpr void EmitControl(USBFunctionEmitter& emitter) const {
		std::size_t count = StreamingCount();
		std::size_t units = 0;
		if (HasMicrophone()) {
			units += UnitBytes(microphone);
		}

		if (HasSpeaker()) {
			units += UnitBytes(speaker);
		}

		std::array<uint8, 10> header{};
		header[0] = static_cast<uint8>(8 + count);
		header[1] = USBAudioClassSpecificInterface;
		header[2] = USBAudioHeaderSubtype;
		USBDescriptorPut16(header, 3, 0x0100);
		USBDescriptorPut16(header, 5, static_cast<uint16>(8 + count + units));
		header[7] = static_cast<uint8>(count);
		std::size_t position = 8;
		if (HasMicrophone()) {
			header[position] = emitter.InterfaceNumber(1);
			position++;
		}

		if (HasSpeaker()) {
			header[position] = emitter.InterfaceNumber(SpeakerIndex());
		}

		emitter.Raw(std::span<const uint8>(header).first(8 + count));

		if (HasMicrophone()) {
			EmitUnits(emitter, microphone, USBAudioMicrophoneInput, USBAudioMicrophoneTerminal, USBAudioMicrophoneFeature, USBAudioStreamingTerminal, USBAudioMicrophoneOutput);
		}

		if (HasSpeaker()) {
			EmitUnits(emitter, speaker, USBAudioSpeakerInput, USBAudioStreamingTerminal, USBAudioSpeakerFeature, USBAudioSpeakerTerminal, USBAudioSpeakerOutput);
		}
	}


	constexpr void EmitUnits(
		USBFunctionEmitter& emitter,
		const PathSettings& path,
		uint8 inputId,
		uint16 inputType,
		uint8 featureId,
		uint16 outputType,
		uint8 outputId) const
	{
		std::array<uint8, 12> input = MakeUSBAudioInputTerminal(inputId, inputType, path.channels, ChannelConfig(path.channels));
		emitter.Raw(input);
		std::array<uint8, 10> feature = MakeUSBAudioFeatureUnit(featureId, inputId, path.channels, MasterControls(path));
		emitter.Raw(std::span<const uint8>(feature).first(feature[0]));
		std::array<uint8, 9> output = MakeUSBAudioOutputTerminal(outputId, outputType, featureId);
		emitter.Raw(output);
	}


	// One AudioStreaming interface: alternate 0 with no endpoints, then alternate 1 with AS_GENERAL, the format, the isochronous
	// endpoint and its class-specific descriptor.
	constexpr void EmitStreaming(
		USBFunctionEmitter& emitter,
		std::size_t index,
		std::size_t endpointIndex,
		const PathSettings& path,
		uint8 terminalLink,
		std::size_t stringIndex) const
	{
		emitter.Interface(index, USBAudioClassCode, USBAudioStreamingSubClass, 0x00, stringIndex);
		emitter.Alternate(index, 1, USBAudioClassCode, USBAudioStreamingSubClass, 0x00, stringIndex);
		std::array<uint8, 7> general = MakeUSBAudioGeneral(terminalLink);
		emitter.Raw(general);
		std::array<uint8, 17> format = MakeUSBAudioFormatType(path.channels, RatesOf(path), RateCount(path));
		emitter.Raw(std::span<const uint8>(format).first(format[0]));
		emitter.AudioEndpoint(endpointIndex);
		std::array<uint8, 7> endpointGeneral = MakeUSBAudioEndpointGeneral(RateCount(path) > 1);
		emitter.Raw(endpointGeneral);
	}
};
