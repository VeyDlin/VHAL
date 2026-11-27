// expect: the packet size holds the largest input report
#include <Drivers/Interface/USB/Class/HID/USBHidBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>

using KeyboardReport = USBHidReportDescriptor<+[] { return USBHidReportBuilder::BootKeyboard(); }>;

using Usb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Hid")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBHidBuilder().BootKeyboard().Report(KeyboardReport::Info).PacketSize(4).In(0x81)));
}>;

static_assert(Usb::Device.size() == 18);
