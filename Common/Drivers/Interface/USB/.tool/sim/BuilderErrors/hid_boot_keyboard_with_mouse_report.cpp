// expect: a boot keyboard sends an 8-byte input report without a report ID
#include <Drivers/Interface/USB/Class/HID/USBHidBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>

using MouseReport = USBHidReportDescriptor<+[] { return USBHidReportBuilder::BootMouse(); }>;

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
			.Function(USBHidBuilder().BootKeyboard().Report(MouseReport::Info).In(0x81)));
}>;

static_assert(Usb::Device.size() == 18);
