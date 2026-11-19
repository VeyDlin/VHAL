// expect: BulkIn needs an IN address (bit 7 set)
#include <Drivers/Interface/USB/Class/MSC/USBMscBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>

using Usb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Mass storage")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBMscBuilder().BulkIn(0x02)));
}>;

static_assert(Usb::Device.size() == 18);
