// expect: an alternate setting follows its alternate 0 directly
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include "AlternateFunction.h"


using Usb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(AlternateFunction<false, 1>()));
}>;

static_assert(Usb::Device.size() == 18);
