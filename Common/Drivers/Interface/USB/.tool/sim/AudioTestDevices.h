#pragma once
#include <VHAL.h>
#include "FixedList.h"
#include "SimAssert.h"
#include "SimHost.h"
#include "SimTestHelpers.h"
#include "SimUSBDeviceAdapter.h"
#include <Drivers/Interface/USB/Class/Audio/AudioFunction.h>
#include <Drivers/Interface/USB/Class/Audio/USBAudioBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <array>
#include <cstddef>
#include <span>


// The audio devices of the host tests. All three have the Audio Control interface 0, the microphone's AudioStreaming interface 1 and the
// speaker's interface 2, with the microphone's isochronous IN endpoint 0x81 and the speaker's isochronous OUT endpoint 0x02. The feature
// units are 2 (microphone) and 5 (speaker). Every interface and endpoint number is fixed here, so the tests name them.
//
// BasicUsb: a mono 48 kHz microphone and a stereo 48 kHz speaker, each with volume and mute.
// MultiRateUsb: the mono microphone lists 16, 32 and 48 kHz, starting at 16 kHz with volume only; the stereo speaker lists 48 and 16 kHz,
//               starting at 48 kHz with mute only.
// NamedUsb: BasicUsb with names on the control interface and on both streams.
// StereoMicUsb: a stereo 48 kHz microphone and a mono 48 kHz speaker, for the ring check on the microphone's channels.
// StereoPairUsb: a stereo 48 kHz microphone and a stereo 48 kHz speaker. Their 196-byte packets take 196-byte IN blocks and 224-byte OUT blocks, two
//               per endpoint. With the buffer table of the numbers 0 to 2 (24 bytes) the pair takes 992 of the 1 KB of packet memory.
namespace AudioTest {


using BasicUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Audio basic")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBAudioBuilder()
				.Microphone(1)
				.MicrophoneAddress(0x81)
				.MicrophoneVolume()
				.MicrophoneMute()
				.Speaker(2)
				.SpeakerAddress(0x02)
				.SpeakerVolume()
				.SpeakerMute()));
}>;


using MultiRateUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Audio rates")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBAudioBuilder()
				.Microphone(1)
				.MicrophoneAddress(0x81)
				.MicrophoneRate(16000)
				.MicrophoneRate(32000)
				.MicrophoneRate(48000)
				.MicrophoneVolume()
				.Speaker(2)
				.SpeakerAddress(0x02)
				.SpeakerRate(48000)
				.SpeakerRate(16000)
				.SpeakerMute()));
}>;


using NamedUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Audio named")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBAudioBuilder()
				.Name("Audio")
				.Microphone(1)
				.MicrophoneAddress(0x81)
				.MicrophoneName("Microphone")
				.MicrophoneVolume()
				.MicrophoneMute()
				.Speaker(2)
				.SpeakerAddress(0x02)
				.SpeakerName("Speaker")
				.SpeakerVolume()
				.SpeakerMute()));
}>;


using StereoMicUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Audio stereo mic")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBAudioBuilder()
				.Microphone(2)
				.MicrophoneAddress(0x81)
				.Speaker(1)
				.SpeakerAddress(0x02)));
}>;


using StereoPairUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Audio stereo pair")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBAudioBuilder()
				.Microphone(2)
				.MicrophoneAddress(0x81)
				.Speaker(2)
				.SpeakerAddress(0x02)));
}>;


// The function's rings: 192 microphone samples (four mono frames at 48 kHz), 128 speaker samples (so two stereo packets overflow).
using Audio = AudioFunction<192, 128>;


// Requests and stream changes the handlers saw, in order. The handlers run in interrupt context in the device, so they only store.
struct StreamEvent {
	USBAudioStream stream = USBAudioStream::Microphone;
	bool active = false;
};


struct FeatureEvent {
	USBAudioStream stream = USBAudioStream::Microphone;
	int16 volume = 0;
	bool muted = false;
};


struct RateEvent {
	USBAudioStream stream = USBAudioStream::Microphone;
	uint32 rate = 0;
};


struct AudioRecorder {
	FixedList<StreamEvent, 16> streams;
	FixedList<FeatureEvent, 16> features;
	FixedList<RateEvent, 16> rates;


	void OnStream(USBAudioStream stream, bool active) {
		StreamEvent event;
		event.stream = stream;
		event.active = active;
		streams.Push(event);
	}


	void OnFeature(USBAudioStream stream, int16 volume, bool muted) {
		FeatureEvent event;
		event.stream = stream;
		event.volume = volume;
		event.muted = muted;
		features.Push(event);
	}


	void OnRate(USBAudioStream stream, uint32 rate) {
		RateEvent event;
		event.stream = stream;
		event.rate = rate;
		rates.Push(event);
	}
};


// A simulated device with the audio function of one of the descriptor sets above, with its handlers recorded. TAudio is the function
// type, Audio by default; a test with other ring sizes names its own. Start() enumerates the device as a host does. The function needs
// no start-of-frame event: its packets are armed by the alternate settings and by the completions.
template<typename TUsb, typename TAudio = Audio>
struct AudioFixture {
	SimUSBDeviceAdapter sim;
	AudioRecorder recorder;
	TAudio audio;
	std::array<USBFunction*, 1> functions;
	USBDevice device;


	AudioFixture()
		: audio(TUsb::Function(0)),
		  functions{ &audio },
		  device(sim, TUsb::Set, functions) {
		SIM_EXPECT_EQ(audio.SetStreamHandler(TAudio::StreamDelegate::template Bind<AudioRecorder, &AudioRecorder::OnStream>(&recorder)), ResultStatus::ok);
		SIM_EXPECT_EQ(audio.SetFeatureHandler(TAudio::FeatureDelegate::template Bind<AudioRecorder, &AudioRecorder::OnFeature>(&recorder)), ResultStatus::ok);
		SIM_EXPECT_EQ(audio.SetRateHandler(TAudio::RateDelegate::template Bind<AudioRecorder, &AudioRecorder::OnRate>(&recorder)), ResultStatus::ok);
	}


	// Every case ends with no critical section open.
	~AudioFixture() {
		SimAssertNoCriticalSection();
	}


	void Start() {
		USBDeviceAdapter::Parameters parameters;
		SIM_EXPECT_EQ(device.Start(parameters), ResultStatus::ok);
		SimTest::EnumerateDevice(sim, device);
	}


	// SET_INTERFACE of an interface (a standard request, bmRequestType 0x01).
	ResultStatus SetInterface(uint8 interfaceNumber, uint8 alternate) {
		return SimHost::Control(sim, SimHost::MakeSetup(0x01, 11, alternate, interfaceNumber, 0));
	}


	// One class request with a data stage in either direction, as the host sends it. The control interface is 0.
	ResultStatus ClassRequest(uint8 requestType, uint8 request, uint16 value, uint16 index, std::span<const uint8> hostData, std::span<uint8> in, uint32& received) {
		return SimHost::Control(sim, SimHost::MakeSetup(requestType, request, value, index, static_cast<uint16>(hostData.size() + in.size())), hostData, in, received);
	}


	// A feature unit request: bmRequestType 0x21 for SET_CUR and 0xA1 for the GET requests, wValue the selector in the high byte and
	// the channel in the low byte, wIndex the unit in the high byte and the control interface in the low byte.
	ResultStatus FeatureSet(uint8 unit, uint8 selector, uint8 channel, std::span<const uint8> value) {
		uint32 received = 0;
		uint16 wValue = static_cast<uint16>((selector << 8) | channel);
		uint16 wIndex = static_cast<uint16>((unit << 8) | 0);
		return SimHost::Control(sim, SimHost::MakeSetup(0x21, USBAudioSetCur, wValue, wIndex, static_cast<uint16>(value.size())), value, std::span<uint8>(), received);
	}


	ResultStatus FeatureGet(uint8 unit, uint8 selector, uint8 channel, uint8 request, std::span<uint8> out, uint32& received) {
		uint16 wValue = static_cast<uint16>((selector << 8) | channel);
		uint16 wIndex = static_cast<uint16>((unit << 8) | 0);
		return SimHost::Control(sim, SimHost::MakeSetup(0xA1, request, wValue, wIndex, static_cast<uint16>(out.size())), std::span<const uint8>(), out, received);
	}


	// Sampling frequency of an isochronous endpoint: bmRequestType 0x22 for SET_CUR and 0xA2 for GET_CUR, wValue 0x0100, wIndex the
	// endpoint address, three bytes of rate, little endian.
	// wLength is the size of the data, so a test can send or ask for a length other than three.
	ResultStatus SamplingSet(uint8 address, std::span<const uint8> rate) {
		uint32 received = 0;
		return SimHost::Control(sim, SimHost::MakeSetup(0x22, USBAudioSetCur, 0x0100, address, static_cast<uint16>(rate.size())), rate, std::span<uint8>(), received);
	}


	ResultStatus SamplingGet(uint8 address, std::span<uint8> out, uint32& received) {
		return SimHost::Control(sim, SimHost::MakeSetup(0xA2, USBAudioGetCur, 0x0100, address, static_cast<uint16>(out.size())), std::span<const uint8>(), out, received);
	}
};


// A pattern of 16-bit samples: value depends on the index and the seed, so a misplaced packet shows.
inline int16 PatternSample(uint32 seed, std::size_t index) {
	return static_cast<int16>(static_cast<uint16>((seed * 977U + index * 31U + 7U) & 0xFFFF));
}


// Little-endian bytes of the samples, as the wire carries them.
template<std::size_t Count>
std::array<uint8, Count * 2> SamplesToBytes(const std::array<int16, Count>& samples) {
	std::array<uint8, Count * 2> bytes{};
	for (std::size_t index = 0; index < Count; index++) {
		uint16 value = static_cast<uint16>(samples[index]);
		bytes[2 * index] = static_cast<uint8>(value & 0xFF);
		bytes[2 * index + 1] = static_cast<uint8>(value >> 8);
	}

	return bytes;
}


}
