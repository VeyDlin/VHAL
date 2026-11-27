// expect: Region() is for a DFU mode interface
#include <Drivers/Interface/USB/Class/DFU/USBDfuBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>


using Usb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0003)
		.DeviceRelease(0x0100)
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBDfuBuilder()
				.Runtime()
				.DetachTimeoutMs(100)
				.TransferSize(64)
				.Region("Flash")));
}>;

static_assert(Usb::Device.size() == 18);
