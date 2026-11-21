// expect: endpoint 0 belongs to the control pipe
#include <Drivers/Interface/USB/Class/CDC/USBCdcAcmBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>

using Usb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBCdcAcmBuilder().NotificationIn(0x83).BulkIn(0x80).BulkOut(0x02)));
}>;

static_assert(Usb::Device.size() == 18);
