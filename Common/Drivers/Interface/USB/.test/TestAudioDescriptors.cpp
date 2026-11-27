#include <VHAL.h>
#include "AudioTestDevices.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <Drivers/Interface/USB/Class/Audio/USBAudioBuilder.h>
#include <array>
#include <cstddef>
#include <span>


// The Audio 1.0 descriptors of the builder, checked byte for byte against arrays written by hand from the Audio Device Class 1.0 and
// Audio Data Formats 1.0 layouts: the Audio Control header with wTotalLength, the input terminals (12 bytes), feature units (8 + channels),
// output terminals (9 bytes), the AS_GENERAL and FORMAT_TYPE I descriptors, the 9-byte isochronous endpoint with bRefresh and bSynchAddress,
// and the 7-byte class-specific endpoint. The expected bytes are not copied from the code's output.
//
// Sample rates are 3 bytes little endian: 16000 is 80 3E 00, 32000 is 00 7D 00 and 48000 is 80 BB 00. wMaxPacketSize is
// (48 + 1) * channels * 2: 98 for mono and 196 for stereo.


namespace {


// Mono 48 kHz microphone with volume and mute, stereo 48 kHz speaker with volume and mute. 201 bytes. wTotalLength of the Audio Control
// header is 71: its 10 bytes, the microphone's units (12 + 9 + 9) and the speaker's (12 + 10 + 9).
constexpr uint8 BasicConfiguration[] = {
	0x09, 0x02, 0xC9, 0x00, 0x03, 0x01, 0x00, 0x80, 0x32,	// Configuration: 3 interfaces, value 1, 100 mA
	0x08, 0x0B, 0x00, 0x03, 0x01, 0x00, 0x00, 0x00,		// Interface association: interfaces 0 to 2, class 0x01/0x00/0x00
	0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,	// Audio Control interface 0: no endpoints, subclass 0x01
	0x0A, 0x24, 0x01, 0x00, 0x01, 0x47, 0x00, 0x02, 0x01, 0x02,	// Header: bcdADC 0x0100, wTotalLength 71, interfaces 1 and 2
	0x0C, 0x24, 0x02, 0x01, 0x01, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,	// Microphone input terminal 1: 0x0201, mono, config 0
	0x09, 0x24, 0x06, 0x02, 0x01, 0x01, 0x03, 0x00, 0x00,	// Microphone feature unit 2: source 1, master volume and mute, channel 0 none
	0x09, 0x24, 0x03, 0x03, 0x01, 0x01, 0x00, 0x02, 0x00,	// Microphone output terminal 3: USB streaming 0x0101, source 2
	0x0C, 0x24, 0x02, 0x04, 0x01, 0x01, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,	// Speaker input terminal 4: USB streaming, stereo, config 0x0003
	0x0A, 0x24, 0x06, 0x05, 0x04, 0x01, 0x03, 0x00, 0x00, 0x00,	// Speaker feature unit 5: source 4, master volume and mute, channels none
	0x09, 0x24, 0x03, 0x06, 0x01, 0x03, 0x00, 0x05, 0x00,	// Speaker output terminal 6: speaker 0x0301, source 5
	0x09, 0x04, 0x01, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00,	// Microphone AudioStreaming 1, alternate 0: no endpoints
	0x09, 0x04, 0x01, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00,	// Microphone AudioStreaming 1, alternate 1: one endpoint
	0x07, 0x24, 0x01, 0x03, 0x00, 0x01, 0x00,	// AS_GENERAL: terminal link 3, delay 0, PCM
	0x0B, 0x24, 0x02, 0x01, 0x01, 0x02, 0x10, 0x01, 0x80, 0xBB, 0x00,	// FORMAT_TYPE I: mono, 2-byte subframes, 16 bits, 48000 Hz
	0x09, 0x05, 0x81, 0x0D, 0x62, 0x00, 0x01, 0x00, 0x00,	// Endpoint 0x81: isochronous, synchronous (bits 3..2 = 11), 98 bytes, bInterval 1, no feedback
	0x07, 0x25, 0x01, 0x00, 0x00, 0x00, 0x00,	// Class-specific endpoint: no sampling frequency control, lock delay 0
	0x09, 0x04, 0x02, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00,	// Speaker AudioStreaming 2, alternate 0: no endpoints
	0x09, 0x04, 0x02, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00,	// Speaker AudioStreaming 2, alternate 1: one endpoint
	0x07, 0x24, 0x01, 0x04, 0x00, 0x01, 0x00,	// AS_GENERAL: terminal link 4, delay 0, PCM
	0x0B, 0x24, 0x02, 0x01, 0x02, 0x02, 0x10, 0x01, 0x80, 0xBB, 0x00,	// FORMAT_TYPE I: stereo, 48000 Hz
	0x09, 0x05, 0x02, 0x09, 0xC4, 0x00, 0x01, 0x00, 0x00,	// Endpoint 0x02: isochronous, adaptive, 196 bytes, bInterval 1
	0x07, 0x25, 0x01, 0x00, 0x00, 0x00, 0x00,	// Class-specific endpoint: no sampling frequency control
};


// Mono microphone at 16, 32 and 48 kHz with volume only; stereo speaker at 48 and 16 kHz with mute only. 210 bytes. The format of the
// microphone takes three rates (17 bytes), and the speaker's takes two (14 bytes). The class-specific endpoints set bit 0.
constexpr uint8 MultiRateConfiguration[] = {
	0x09, 0x02, 0xD2, 0x00, 0x03, 0x01, 0x00, 0x80, 0x32,	// Configuration: 3 interfaces, value 1, 100 mA
	0x08, 0x0B, 0x00, 0x03, 0x01, 0x00, 0x00, 0x00,		// Interface association: interfaces 0 to 2
	0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,	// Audio Control interface 0
	0x0A, 0x24, 0x01, 0x00, 0x01, 0x47, 0x00, 0x02, 0x01, 0x02,	// Header: wTotalLength 71
	0x0C, 0x24, 0x02, 0x01, 0x01, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,	// Microphone input terminal 1
	0x09, 0x24, 0x06, 0x02, 0x01, 0x01, 0x02, 0x00, 0x00,	// Microphone feature unit 2: master volume only
	0x09, 0x24, 0x03, 0x03, 0x01, 0x01, 0x00, 0x02, 0x00,	// Microphone output terminal 3
	0x0C, 0x24, 0x02, 0x04, 0x01, 0x01, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,	// Speaker input terminal 4: stereo
	0x0A, 0x24, 0x06, 0x05, 0x04, 0x01, 0x01, 0x00, 0x00, 0x00,	// Speaker feature unit 5: master mute only
	0x09, 0x24, 0x03, 0x06, 0x01, 0x03, 0x00, 0x05, 0x00,	// Speaker output terminal 6
	0x09, 0x04, 0x01, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00,	// Microphone AudioStreaming 1, alternate 0
	0x09, 0x04, 0x01, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00,	// Microphone AudioStreaming 1, alternate 1
	0x07, 0x24, 0x01, 0x03, 0x00, 0x01, 0x00,	// AS_GENERAL: terminal link 3
	0x11, 0x24, 0x02, 0x01, 0x01, 0x02, 0x10, 0x03,
	0x80, 0x3E, 0x00,	// 16000 Hz
	0x00, 0x7D, 0x00,	// 32000 Hz
	0x80, 0xBB, 0x00,	// 48000 Hz
	0x09, 0x05, 0x81, 0x0D, 0x62, 0x00, 0x01, 0x00, 0x00,	// Endpoint 0x81: synchronous, 98 bytes
	0x07, 0x25, 0x01, 0x01, 0x00, 0x00, 0x00,	// Class-specific endpoint: sampling frequency control
	0x09, 0x04, 0x02, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00,	// Speaker AudioStreaming 2, alternate 0
	0x09, 0x04, 0x02, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00,	// Speaker AudioStreaming 2, alternate 1
	0x07, 0x24, 0x01, 0x04, 0x00, 0x01, 0x00,	// AS_GENERAL: terminal link 4
	0x0E, 0x24, 0x02, 0x01, 0x02, 0x02, 0x10, 0x02,
	0x80, 0xBB, 0x00,	// 48000 Hz
	0x80, 0x3E, 0x00,	// 16000 Hz
	0x09, 0x05, 0x02, 0x09, 0xC4, 0x00, 0x01, 0x00, 0x00,	// Endpoint 0x02: 196 bytes, adaptive
	0x07, 0x25, 0x01, 0x01, 0x00, 0x00, 0x00,	// Class-specific endpoint: sampling frequency control
};


// The names of NamedUsb: the strings the configuration numbers, as the string descriptors of the device table hold them.
constexpr uint8 AudioName[] = {
	0x0C, 0x03, 0x41, 0x00, 0x75, 0x00, 0x64, 0x00, 0x69, 0x00, 0x6F, 0x00,	// "Audio"
};


constexpr uint8 MicrophoneName[] = {
	0x16, 0x03, 0x4D, 0x00, 0x69, 0x00, 0x63, 0x00, 0x72, 0x00, 0x6F, 0x00, 0x70, 0x00, 0x68, 0x00, 0x6F, 0x00, 0x6E, 0x00, 0x65, 0x00,	// "Microphone"
};


constexpr uint8 SpeakerName[] = {
	0x10, 0x03, 0x53, 0x00, 0x70, 0x00, 0x65, 0x00, 0x61, 0x00, 0x6B, 0x00, 0x65, 0x00, 0x72, 0x00,	// "Speaker"
};


// A stereo microphone written by hand, with no feature unit, listing 16 and 48 kHz. Its isochronous IN endpoint 0x81 is the one the
// layout names. The 48 kHz rate needs 48 frames of 2 channels of 2 bytes, 192 bytes, so the endpoint takes a wMaxPacketSize of 192.
constexpr uint8 MicrophoneHandWritten192[] = {
	0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,	// Audio Control interface 0: no endpoints, subclass 0x01
	0x09, 0x24, 0x01, 0x00, 0x01, 0x15, 0x00, 0x01, 0x01,	// Header: bcdADC 0x0100, wTotalLength 21, interface 1
	0x0C, 0x24, 0x02, 0x01, 0x01, 0x02, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,	// Microphone input terminal 1: 0x0201, stereo, left and right
	0x09, 0x04, 0x01, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00,	// Streaming interface 1, alternate 0: no endpoints
	0x09, 0x04, 0x01, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00,	// Streaming interface 1, alternate 1: one endpoint
	0x07, 0x24, 0x01, 0x03, 0x00, 0x01, 0x00,	// AS_GENERAL: terminal link 3 (the microphone output), PCM
	0x0E, 0x24, 0x02, 0x01, 0x02, 0x02, 0x10, 0x02, 0x80, 0xBB, 0x00, 0x80, 0x3E, 0x00,	// FORMAT_TYPE I: stereo, 2 bytes, 48000 and 16000
	0x09, 0x05, 0x81, 0x05, 0xC0, 0x00, 0x01, 0x00, 0x00,	// Endpoint 0x81: isochronous, asynchronous, wMaxPacketSize 192, bInterval 1
};


// The same microphone with wMaxPacketSize 128. The 48 kHz rate is still listed, so the layout cannot carry the packets it asks for.
constexpr uint8 MicrophoneHandWritten128[] = {
	0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,	// Audio Control interface 0: no endpoints, subclass 0x01
	0x09, 0x24, 0x01, 0x00, 0x01, 0x15, 0x00, 0x01, 0x01,	// Header: bcdADC 0x0100, wTotalLength 21, interface 1
	0x0C, 0x24, 0x02, 0x01, 0x01, 0x02, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,	// Microphone input terminal 1: 0x0201, stereo, left and right
	0x09, 0x04, 0x01, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00,	// Streaming interface 1, alternate 0: no endpoints
	0x09, 0x04, 0x01, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00,	// Streaming interface 1, alternate 1: one endpoint
	0x07, 0x24, 0x01, 0x03, 0x00, 0x01, 0x00,	// AS_GENERAL: terminal link 3 (the microphone output), PCM
	0x0E, 0x24, 0x02, 0x01, 0x02, 0x02, 0x10, 0x02, 0x80, 0xBB, 0x00, 0x80, 0x3E, 0x00,	// FORMAT_TYPE I: stereo, 2 bytes, 48000 and 16000
	0x09, 0x05, 0x81, 0x05, 0x80, 0x00, 0x01, 0x00, 0x00,	// Endpoint 0x81: isochronous, asynchronous, wMaxPacketSize 128, bInterval 1
};


// A stereo speaker written by hand, listing 48 kHz only. The speaker is an adaptive sink, so its host may send 49 frames a millisecond. That
// packet is 49 * 2 * 2 = 196 bytes, and the endpoint 0x02 must take it. A wMaxPacketSize of 192 holds only the 48 frames of the rate.
constexpr uint8 SpeakerHandWritten196[] = {
	0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,	// Audio Control interface 0: no endpoints, subclass 0x01
	0x09, 0x24, 0x01, 0x00, 0x01, 0x09, 0x00, 0x01, 0x01,	// Header: bcdADC 0x0100, wTotalLength 9, interface 1
	0x09, 0x04, 0x01, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00,	// Streaming interface 1, alternate 0: no endpoints
	0x09, 0x04, 0x01, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00,	// Streaming interface 1, alternate 1: one endpoint
	0x07, 0x24, 0x01, 0x04, 0x00, 0x01, 0x00,	// AS_GENERAL: terminal link 4 (the speaker input), PCM
	0x0B, 0x24, 0x02, 0x01, 0x02, 0x02, 0x10, 0x01, 0x80, 0xBB, 0x00,	// FORMAT_TYPE I: stereo, 2 bytes, 48000 only
	0x09, 0x05, 0x02, 0x0D, 0xC4, 0x00, 0x01, 0x00, 0x00,	// Endpoint 0x02: isochronous, adaptive, wMaxPacketSize 196, bInterval 1
};


// The same speaker with wMaxPacketSize 192, which holds the 48 frames of the rate and not the extra frame.
constexpr uint8 SpeakerHandWritten192[] = {
	0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,	// Audio Control interface 0: no endpoints, subclass 0x01
	0x09, 0x24, 0x01, 0x00, 0x01, 0x09, 0x00, 0x01, 0x01,	// Header: bcdADC 0x0100, wTotalLength 9, interface 1
	0x09, 0x04, 0x01, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00,	// Streaming interface 1, alternate 0: no endpoints
	0x09, 0x04, 0x01, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00,	// Streaming interface 1, alternate 1: one endpoint
	0x07, 0x24, 0x01, 0x04, 0x00, 0x01, 0x00,	// AS_GENERAL: terminal link 4 (the speaker input), PCM
	0x0B, 0x24, 0x02, 0x01, 0x02, 0x02, 0x10, 0x01, 0x80, 0xBB, 0x00,	// FORMAT_TYPE I: stereo, 2 bytes, 48000 only
	0x09, 0x05, 0x02, 0x0D, 0xC0, 0x00, 0x01, 0x00, 0x00,	// Endpoint 0x02: isochronous, adaptive, wMaxPacketSize 192, bInterval 1
};


// A context for OnConfigured(). The configure check reads no call of it, so every call is refused.
struct NullContext : public USBDeviceContext {
	ResultStatus OpenEndpoint(const USBDeviceAdapter::EndpointConfig&) override {
		return ResultStatus::notReady;
	}


	ResultStatus CloseEndpoint(uint8) override {
		return ResultStatus::notReady;
	}


	ResultStatus StartInTransfer(uint8, std::span<const uint8>, bool) override {
		return ResultStatus::notReady;
	}


	ResultStatus StartOutTransfer(uint8, std::span<uint8>) override {
		return ResultStatus::notReady;
	}


	ResultStatus Stall(uint8) override {
		return ResultStatus::notReady;
	}


	ResultStatus ClearStall(uint8) override {
		return ResultStatus::notReady;
	}


	ResultStatus RemoteWakeup() override {
		return ResultStatus::notReady;
	}


	ResultStatus ControlSendData(std::span<const uint8>) override {
		return ResultStatus::notReady;
	}


	ResultStatus ControlReceiveData(std::span<uint8>) override {
		return ResultStatus::notReady;
	}
};


// The configure result of an audio function over a hand-written slice: the layout names interfaces 0 and 1 and the one endpoint.
ResultStatus ConfigureWith(std::span<const uint8> descriptors, std::span<const uint8> endpoints) {
	constexpr std::array<uint8, 2> interfaces = { 0, 1 };
	USBFunctionLayout layout;
	layout.interfaces = interfaces;
	layout.endpoints = endpoints;
	layout.descriptors = descriptors;
	NullContext context;
	AudioTest::Audio audio(layout);
	return audio.OnConfigured(context);
}


// Position of the first occurrence of pattern in bytes, or bytes.size() when it is not there.
std::size_t FindPattern(std::span<const uint8> bytes, std::span<const uint8> pattern) {
	for (std::size_t start = 0; start + pattern.size() <= bytes.size(); start++) {
		if (SimHost::SameBytes(bytes.subspan(start, pattern.size()), pattern)) {
			return start;
		}
	}

	return bytes.size();
}


// The string index whose descriptor is expected, or the table size when no entry has it.
std::size_t FindStringIndex(std::span<const std::span<const uint8>> table, std::span<const uint8> expected) {
	for (std::size_t index = 0; index < table.size(); index++) {
		if (SimHost::SameBytes(table[index], expected)) {
			return index;
		}
	}

	return table.size();
}


}


// The basic device's configuration, byte for byte.
void TestBasicConfigurationBytes() {
	SIM_EXPECT_BYTES(AudioTest::BasicUsb::Configuration, BasicConfiguration);
}


// The multi-rate device's configuration, byte for byte.
void TestMultiRateConfigurationBytes() {
	SIM_EXPECT_BYTES(AudioTest::MultiRateUsb::Configuration, MultiRateConfiguration);
}


// The layout the function receives: interfaces 0 to 2, endpoints in the order the microphone then the speaker, and the descriptors from
// the interface association to the end of the configuration.
void TestFunctionLayout() {
	USBFunctionLayout layout = AudioTest::BasicUsb::Function(0);
	SIM_EXPECT_EQ(layout.interfaces.size(), 3U);
	SIM_EXPECT_EQ(layout.interfaces[0], 0);
	SIM_EXPECT_EQ(layout.interfaces[1], 1);
	SIM_EXPECT_EQ(layout.interfaces[2], 2);
	SIM_EXPECT_EQ(layout.endpoints.size(), 2U);
	SIM_EXPECT_EQ(layout.endpoints[0], 0x81);
	SIM_EXPECT_EQ(layout.endpoints[1], 0x02);
	SIM_EXPECT_EQ(layout.descriptors.size(), sizeof(BasicConfiguration) - 9);
	SIM_EXPECT(layout.runtimeStrings.empty());
}


// Names: the control interface and the two streams point at their strings, and the strings hold the names given. The name of a
// stream is in its alternate 0 interface, so the byte found after the pattern is the string index.
void TestNamesPointAtStrings() {
	const std::array<uint8, 8> acPattern = { 0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00 };
	const std::array<uint8, 8> micPattern = { 0x09, 0x04, 0x01, 0x00, 0x00, 0x01, 0x02, 0x00 };
	const std::array<uint8, 8> spkPattern = { 0x09, 0x04, 0x02, 0x00, 0x00, 0x01, 0x02, 0x00 };
	std::span<const uint8> configuration = AudioTest::NamedUsb::Configuration;
	std::span<const std::span<const uint8>> table = AudioTest::NamedUsb::StringTable;

	std::size_t acAt = FindPattern(configuration, acPattern);
	SIM_EXPECT(acAt < configuration.size());
	if (acAt < configuration.size()) {
		SIM_EXPECT_EQ(configuration[acAt + 8], FindStringIndex(table, AudioName));
		SIM_EXPECT(configuration[acAt + 8] != 0);
	}

	std::size_t micAt = FindPattern(configuration, micPattern);
	SIM_EXPECT(micAt < configuration.size());
	if (micAt < configuration.size()) {
		SIM_EXPECT_EQ(configuration[micAt + 8], FindStringIndex(table, MicrophoneName));
		SIM_EXPECT(configuration[micAt + 8] != 0);
	}

	std::size_t spkAt = FindPattern(configuration, spkPattern);
	SIM_EXPECT(spkAt < configuration.size());
	if (spkAt < configuration.size()) {
		SIM_EXPECT_EQ(configuration[spkAt + 8], FindStringIndex(table, SpeakerName));
		SIM_EXPECT(configuration[spkAt + 8] != 0);
	}
}


// A stream without a name leaves its iInterface at 0, as the basic device does.
void TestUnnamedStreamsHaveNoString() {
	const std::array<uint8, 8> micPattern = { 0x09, 0x04, 0x01, 0x00, 0x00, 0x01, 0x02, 0x00 };
	std::span<const uint8> configuration = AudioTest::BasicUsb::Configuration;
	std::size_t micAt = FindPattern(configuration, micPattern);
	SIM_EXPECT(micAt < configuration.size());
	if (micAt < configuration.size()) {
		SIM_EXPECT_EQ(configuration[micAt + 8], 0);
	}
}


// A stereo microphone written by hand that lists 48 kHz needs 192 bytes from its endpoint. The endpoint with 192 configures the function, and
// the one with 128 is refused: the port would refuse every arm at that rate, and the stream would idle.
void TestHandWrittenMicrophoneRateNeedsEndpointPacket() {
	constexpr std::array<uint8, 1> micEndpoints = { 0x81 };
	SIM_EXPECT_EQ(ConfigureWith(MicrophoneHandWritten192, micEndpoints), ResultStatus::ok);
	SIM_EXPECT_EQ(ConfigureWith(MicrophoneHandWritten128, micEndpoints), ResultStatus::invalidParameter);
}


// The speaker is an adaptive sink, so its host may send one frame more than the rate: 49 * 2 * 2 = 196 bytes at 48 kHz. The endpoint with 196
// configures the function, and the one with 192, which holds only the 48 frames, is refused.
void TestHandWrittenSpeakerExtraFrameNeedsEndpointPacket() {
	constexpr std::array<uint8, 1> spkEndpoints = { 0x02 };
	SIM_EXPECT_EQ(ConfigureWith(SpeakerHandWritten196, spkEndpoints), ResultStatus::ok);
	SIM_EXPECT_EQ(ConfigureWith(SpeakerHandWritten192, spkEndpoints), ResultStatus::invalidParameter);
}


int main() {
	TestBasicConfigurationBytes();
	TestMultiRateConfigurationBytes();
	TestFunctionLayout();
	TestNamesPointAtStrings();
	TestUnnamedStreamsHaveNoString();
	TestHandWrittenMicrophoneRateNeedsEndpointPacket();
	TestHandWrittenSpeakerExtraFrameNeedsEndpointPacket();
	return SimTest::Finish("TestAudioDescriptors");
}
