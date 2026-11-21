// expect: NotificationIn needs an IN address
#include <Drivers/Interface/USB/Class/CDC/USBCdcNcmBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>

using Usb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0002)
		.DeviceRelease(0x0100)
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBCdcNcmBuilder().NotificationIn(0x03).BulkIn(0x81).BulkOut(0x02)));
}>;

static_assert(Usb::Device.size() == 18);
