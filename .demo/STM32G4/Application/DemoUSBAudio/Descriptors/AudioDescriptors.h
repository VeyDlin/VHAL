#pragma once
#include <DemoProducts.h>
#include <Drivers/Interface/USB/Class/Audio/USBAudioBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBConfigurationBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorComposition.h>


// The descriptors of the audio device: one USB Audio 1.0 function with a microphone (mono, isochronous IN 0x81) and a speaker
// (stereo, isochronous OUT 0x02), both at 48 kHz with volume and mute. Windows binds the in-box usbaudio.sys driver to the audio class
// codes, Linux snd-usb-audio, and macOS its own class driver, so there is no INF and no WinUSB.
//
// The order of the calls sets the string indexes: manufacturer 1, product 2, the serial number 3 (filled in at run time from the MCU's
// unique ID), and then the names of the function and its streams. The product string contains "VHAL", so the device is easy to find
// in the sound settings; the exact name the host shows is checked on the board, not assumed here.
using AudioUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(DemoProducts::VendorId)
		.ProductId(DemoProducts::AudioProductId)
		.DeviceRelease(DemoProducts::DeviceRelease)
		.Manufacturer("VHAL")
		.Product("VHAL Audio Demo")
		.SerialNumberAtRuntime()
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBAudioBuilder()
				.Name("VHAL audio")
				.Microphone(1)
				.MicrophoneAddress(0x81)
				.MicrophoneName("VHAL microphone")
				.MicrophoneVolume()
				.MicrophoneMute()
				.Speaker(2)
				.SpeakerAddress(0x02)
				.SpeakerName("VHAL speaker")
				.SpeakerVolume()
				.SpeakerMute()));
}>;


// The device's identity, checked against the shared IDs. Offsets: idVendor 8, idProduct 10, bcdDevice 12 in the device descriptor.
static_assert(USBDescriptorRead16(AudioUsb::Device, 8) == DemoProducts::VendorId, "idVendor must be DemoProducts::VendorId");
static_assert(USBDescriptorRead16(AudioUsb::Device, 10) == DemoProducts::AudioProductId, "idProduct must be DemoProducts::AudioProductId");
static_assert(USBDescriptorRead16(AudioUsb::Device, 12) == DemoProducts::DeviceRelease, "bcdDevice must be DemoProducts::DeviceRelease");
// The configuration has three interfaces (the Audio Control interface and the two AudioStreaming interfaces), at offset 4.
static_assert(AudioUsb::Configuration[4] == 3, "the configuration has three interfaces");
static_assert(USBDeviceClassMatchesConfiguration(AudioUsb::Device, AudioUsb::Configuration), "device class must match the configuration");
