// expect: too many run-time strings in one configuration
#include "../RuntimeStringFunction.h"
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
			.Function(RuntimeStringFunction<3, 1>())
			.Function(RuntimeStringFunction<3, 2>()));
}>;

static_assert(Usb::Device.size() == 18);
