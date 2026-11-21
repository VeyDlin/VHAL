#pragma once
#include <DemoProducts.h>
#include <Drivers/Interface/USB/Class/DFU/USBDfuBuilder.h>
#include <Drivers/Interface/USB/Class/DFU/USBDfuProtocol.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBConfigurationBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include <Drivers/Interface/USB/Descriptor/MsOs20/USBMsOs20DescriptorSet.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorComposition.h>


namespace DemoUSBDfu {
	// The block size of the DFU mode function: the largest DFU_DNLOAD block and DFU_UPLOAD answer. The function's block buffer is
	// this long too. dfu-util takes it from the functional descriptor (-t can only lower it).
	constexpr uint16 DfuTransferSize = 2048;


	// The interface string of the region. dfu-util -l shows it, so it says where the region is and how big it is. It must match
	// VHAL_DFU_REGION_START and VHAL_DFU_REGION_SIZE in CMakeLists.txt.
	constexpr const char* DfuRegionName = "Upper flash 0x08018000 32 KB";
}


// The descriptors of the DFU demo: a DFU 1.1 mode device with one memory region, the upper flash, and WinUSB through the MS OS 2.0
// descriptors, so dfu-util opens it on Windows without Zadig. The device stays on the bus after a download (manifestation
// tolerant): the demo does not restart into the new image, it only fills the region the image does not use.
//
// The order of the calls sets the string indexes: manufacturer 1, product 2, the serial number 3 and the interface name 4. The serial
// number string is filled in at run time by USBDevice::SetSerialNumber() from the MCU's unique ID (DemoUSBDfu.cpp).
using DfuUsb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(DemoProducts::VendorId)
		.ProductId(DemoProducts::DfuProductId)
		.DeviceRelease(DemoProducts::DeviceRelease)
		.Manufacturer("VHAL")
		.Product("USB DFU")
		.SerialNumberAtRuntime()
		.MsOs20VendorCode(DemoProducts::DfuMsOs20VendorCode)
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBDfuBuilder()
				.Mode()
				.CanDnload()
				.CanUpload()
				.ManifestationTolerant()
				.TransferSize(DemoUSBDfu::DfuTransferSize)
				.Region(DemoUSBDfu::DfuRegionName)
				.WinUsb(DemoProducts::DfuInterfaceGuid)));
}>;


// dfu-util and the Windows binding find the device by these values, and the host's requests are answered from these bytes. The asserts
// read the bytes the core will serve, so a value written at the wrong offset fails the build.
// Offsets: device descriptor idVendor 8, idProduct 10, bcdDevice 12. Configuration: the DFU interface starts at 9, so its class,
// subclass and protocol are at 9 + 5, 9 + 6 and 9 + 7. The functional descriptor follows the interface at 18: bmAttributes is at
// 18 + 2 and wTransferSize at 18 + 5.
static_assert(USBDescriptorRead16(DfuUsb::Device, 8) == DemoProducts::VendorId, "idVendor must be DemoProducts::VendorId");
static_assert(USBDescriptorRead16(DfuUsb::Device, 10) == DemoProducts::DfuProductId, "idProduct must be DemoProducts::DfuProductId");
static_assert(USBDescriptorRead16(DfuUsb::Device, 12) == DemoProducts::DeviceRelease, "bcdDevice must be DemoProducts::DeviceRelease");
static_assert(DfuUsb::Configuration[14] == 0xFE, "the DFU interface class is application specific (0xFE)");
static_assert(DfuUsb::Configuration[15] == DfuInterfaceSubClass, "the DFU interface subclass is 0x01");
static_assert(DfuUsb::Configuration[16] == DfuModeProtocol, "the DFU mode interface protocol is 0x02");
static_assert(
	DfuUsb::Configuration[20] == (DfuAttributeCanDnload | DfuAttributeCanUpload | DfuAttributeManifestationTolerant),
	"bmAttributes: download, upload and manifestation tolerant, no WillDetach"
);
static_assert(USBDescriptorRead16(DfuUsb::Configuration, 23) == DemoUSBDfu::DfuTransferSize, "wTransferSize must be DfuTransferSize");
// The MS OS 2.0 vendor code is in the BOS: 5 bytes of BOS header, then 20 bytes of platform capability header and UUID, then 8
// bytes of its data (dwWindowsVersion, wMSOSDescriptorSetTotalLength, bMS_VendorCode, bAltEnumCode). bMS_VendorCode is at 5 + 20 + 6 = 31.
static_assert(DfuUsb::Bos[31] == DemoProducts::DfuMsOs20VendorCode, "the BOS must carry DemoProducts::DfuMsOs20VendorCode");
static_assert(DfuUsb::VendorRequests.size() == 1, "one vendor request, the MS OS 2.0 descriptor");
static_assert(DfuUsb::VendorRequests[0].request == DemoProducts::DfuMsOs20VendorCode, "the vendor request must be DfuMsOs20VendorCode");
static_assert(DfuUsb::VendorRequests[0].index == USBMsOs20DescriptorIndex, "the MS OS 2.0 descriptor is requested with wIndex 7");
static_assert(USBDeviceClassMatchesConfiguration(DfuUsb::Device, DfuUsb::Configuration), "device class must match the configuration");
static_assert(USBDeviceSupportsBos(DfuUsb::Device), "a BOS needs bcdUSB 2.01 or later");
