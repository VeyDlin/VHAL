// expect: alternate settings are listed in ascending order
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include "AlternateFunction.h"


using Usb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(AlternateFunction<true, 2, 1>()));
}>;

static_assert(Usb::Device.size() == 18);
