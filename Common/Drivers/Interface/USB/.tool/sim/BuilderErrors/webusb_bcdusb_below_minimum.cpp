// expect: WebUsb needs bcdUSB 0x0210 or later
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
		.UsbVersion(0x0201)
		.WebUsb(0x43, "https://example.com/")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBVendorBuilder().BulkIn(0x81).BulkOut(0x02)));
}>;

static_assert(Usb::Device.size() == 18);
