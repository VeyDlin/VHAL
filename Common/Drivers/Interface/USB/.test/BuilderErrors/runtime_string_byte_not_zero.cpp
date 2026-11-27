// expect: the run-time string index byte is given as 0 and filled in by the device
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
			.Function(RuntimeStringFunction<1, 1, true, 0, 0x05>()));
}>;

static_assert(Usb::Device.size() == 18);
