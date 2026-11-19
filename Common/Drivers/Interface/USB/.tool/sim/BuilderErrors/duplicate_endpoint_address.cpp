// expect: duplicate endpoint address
#include <Drivers/Interface/USB/Class/Vendor/USBVendorBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>

using Usb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBVendorBuilder().BulkIn(0x81).BulkOut(0x02))
			.Function(USBVendorBuilder().BulkIn(0x81).BulkOut(0x03)));
}>;

static_assert(Usb::Device.size() == 18);
