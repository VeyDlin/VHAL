// expect: endpoint address bits 6..4 are reserved
#include <Drivers/Interface/USB/Class/Vendor/USBVendorBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>

using Usb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("VHAL")
		.Product("USB")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBVendorBuilder().BulkIn(0x91).BulkOut(0x02)));
}>;

static_assert(Usb::Device.size() == 18);
