// expect: Name() and Region() are not both given
#include <Drivers/Interface/USB/Class/DFU/USBDfuBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>


using Usb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0002)
		.DeviceRelease(0x0100)
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBDfuBuilder()
				.Mode()
				.CanDnload()
				.TransferSize(64)
				.Name("Flash")
				.Region("Config")));
}>;

static_assert(Usb::Device.size() == 18);
