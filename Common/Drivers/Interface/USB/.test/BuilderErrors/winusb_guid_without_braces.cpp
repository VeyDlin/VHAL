// expect: a WinUSB GUID is {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX} in braces
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
		.MsOs20VendorCode(0x42)
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBVendorBuilder().BulkIn(0x81).BulkOut(0x02).WinUsb("9CFFEE21-8C31-4DD2-ACB1-38A9CEB05DA4")));
}>;

static_assert(Usb::Device.size() == 18);
