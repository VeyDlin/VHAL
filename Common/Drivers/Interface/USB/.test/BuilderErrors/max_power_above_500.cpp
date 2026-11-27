// expect: bMaxPower is at most 500 mA
#include <Drivers/Interface/USB/Class/Vendor/USBVendorBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>

using Usb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(600)
			.Function(USBVendorBuilder().BulkIn(0x81).BulkOut(0x02)));
}>;

static_assert(Usb::Device.size() == 18);
