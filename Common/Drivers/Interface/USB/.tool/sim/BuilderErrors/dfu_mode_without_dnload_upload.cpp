// expect: a DFU mode interface downloads or uploads
#include <Drivers/Interface/USB/Class/DFU/USBDfuBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>


using Usb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Dfu")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBDfuBuilder()
				.Mode()
				.DetachTimeoutMs(100)
				.TransferSize(64))));
}>;

static_assert(Usb::Device.size() == 18);
