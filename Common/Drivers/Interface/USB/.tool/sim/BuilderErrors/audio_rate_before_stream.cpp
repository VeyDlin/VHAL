// expect: MicrophoneRate needs Microphone() first
#include <Drivers/Interface/USB/Class/Audio/USBAudioBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>

using Usb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("VHAL")
		.Product("Audio")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBAudioBuilder()
				.MicrophoneRate(48000)
				.Microphone(1)
				.MicrophoneAddress(0x81)));
}>;

static_assert(Usb::Device.size() == 18);
