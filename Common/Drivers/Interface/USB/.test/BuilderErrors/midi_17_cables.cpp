// expect: a USB-MIDI function has 1 to 16 cables
#include <Drivers/Interface/USB/Class/MIDI/USBMidiBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>

using Usb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("VHAL")
		.Product("MIDI")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBMidiBuilder()
				.Cables(17)
				.BulkOut(0x01)
				.BulkIn(0x81)));
}>;

static_assert(Usb::Device.size() == 18);
