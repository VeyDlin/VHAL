#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string_view>
#include <VHAL.h>
#include <Drivers/Interface/USB/Descriptor/MsOs20/USBMsOs20DescriptorSet.h>
#include <Drivers/Interface/USB/Descriptor/MsOs20/USBMsOs20PlatformCapability.h>
#include <Drivers/Interface/USB/Descriptor/USBBosDescriptor.h>
#include <Drivers/Interface/USB/Descriptor/USBConfigurationDescriptor.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorComposition.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorSet.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorString.h>
#include <Drivers/Interface/USB/Descriptor/USBDeviceDescriptor.h>
#include "../common/BenchProtocol.h"
#include "Descriptors.h"
#include "SimAssert.h"

namespace {


// Copies a hand-written C array into a std::array, so a byte comparison can be a static_assert.
template<std::size_t Size>
constexpr std::array<uint8, Size> ToArray(const uint8 (&bytes)[Size]) {
	std::array<uint8, Size> copy{};
	for (std::size_t index = 0; index < Size; index++) {
		copy[index] = bytes[index];
	}

	return copy;
}


// Copies Count bytes of a hand-written array, starting at offset, to compare one element of a larger descriptor.
template<std::size_t Count, std::size_t Size>
constexpr std::array<uint8, Count> Slice(const uint8 (&bytes)[Size], std::size_t offset) {
	std::array<uint8, Count> copy{};
	for (std::size_t index = 0; index < Count; index++) {
		copy[index] = bytes[offset + index];
	}

	return copy;
}


// Prints bytes as hex, 16 to a row, so a mismatch can be read against the other dump.
void PrintBytes(const char* label, std::span<const uint8> bytes) {
	std::fprintf(stderr, "  %s (%zu bytes):", label, bytes.size());
	for (std::size_t index = 0; index < bytes.size(); index++) {
		if (index % 16 == 0) {
			std::fprintf(stderr, "\n    %04zx:", index);
		}

		std::fprintf(stderr, " %02X", static_cast<unsigned int>(bytes[index]));
	}

	std::fprintf(stderr, "\n");
}


// Compares a builder result with the expected bytes. A mismatch prints the first differing offset and both dumps.
void ExpectBytes(const char* name, std::span<const uint8> built, std::span<const uint8> expected) {
	bool same = built.size() == expected.size() && std::equal(built.begin(), built.end(), expected.begin());
	if (same) {
		return;
	}

	std::size_t firstDifference = 0;
	while (firstDifference < built.size() && firstDifference < expected.size() && built[firstDifference] == expected[firstDifference]) {
		firstDifference++;
	}

	std::fprintf(stderr, "%s differs, first difference at offset %zu\n", name, firstDifference);
	PrintBytes("built", built);
	PrintBytes("expected", expected);
	SimTest::Expect(false, name, __FILE__, __LINE__);
}


// The vendor code of the MS OS 2.0 descriptor request as Descriptors.h spells it (bRequest of VendorRequests[0] and bMS_VendorCode
// of its BOS).
constexpr uint8 DescriptorVendorCode = 0x42;


// bAltEnumCode 0: the device does not use alternate enumeration.
constexpr uint8 NoAlternateEnumerationCode = 0x00;


// The bMS_VendorCode of the specification's Example1 platform capability.
constexpr uint8 SpecExampleVendorCode = 0x01;


// Hand-written from the Microsoft OS 2.0 Descriptors Specification, the set header and the compatible ID feature as one
// 30-byte set: wLength 10, wDescriptorType 0, dwWindowsVersion 0x06030000 (Windows 8.1), wTotalLength 30. The set Descriptors.h
// serves is compared with these bytes in main(), so its header is checked as well as its compatible ID.
constexpr uint8 SpecTopLevelSet[] = {
	0x0A, 0x00,											// set header: wLength 10
	0x00, 0x00,											// wDescriptorType 0, MS_OS_20_SET_HEADER_DESCRIPTOR
	0x00, 0x00, 0x03, 0x06,								// dwWindowsVersion 0x06030000 (Windows 8.1)
	0x1E, 0x00,											// wTotalLength 30
	0x14, 0x00, 0x03, 0x00,								// compatible ID feature, 20 bytes
	0x57, 0x49, 0x4E, 0x55, 0x53, 0x42, 0x00, 0x00,		// CompatibleID "WINUSB", zero-padded to 8
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00		// SubCompatibleID, empty
};


// Hand-written from the USB 2.0 chapter 9 layout: the 18-byte device descriptor of the composite device, with the class
// 0xEF, 0x02, 0x01 and otherwise the fields of SimDescriptors::DeviceDescriptor (bcdUSB 2.01, as the builder call uses).
constexpr uint8 SpecIadDevice[] = {
	0x12, 0x01,											// bLength 18, bDescriptorType 1
	0x01, 0x02,											// bcdUSB 2.01
	0xEF, 0x02, 0x01,									// bDeviceClass, bDeviceSubClass, bDeviceProtocol: interface association
	0x40,												// bMaxPacketSize0 64
	0x09, 0x12,											// idVendor 0x1209
	0x01, 0x00,											// idProduct 0x0001
	0x00, 0x01,											// bcdDevice 1.00
	0x01, 0x02, 0x03,									// iManufacturer, iProduct, iSerialNumber
	0x01												// bNumConfigurations
};


// Hand-written from the Microsoft OS 2.0 Descriptors Specification, Compatible ID descriptor (table "Microsoft OS 2.0
// compatible ID descriptor"): wLength 20, wDescriptorType 3, CompatibleID "WINUSB", SubCompatibleID empty.
constexpr uint8 SpecCompatibleId[] = {
	0x14, 0x00,											// wLength 20
	0x03, 0x00,											// wDescriptorType 3, MS_OS_FEATURE_COMPATIBLE_ID
	0x57, 0x49, 0x4E, 0x55, 0x53, 0x42, 0x00, 0x00,		// CompatibleID "WINUSB", zero-padded to 8
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00		// SubCompatibleID, empty
};


// Hand-written from the specification's Example1 (registry value SelectiveSuspendEnabled): the 62-byte registry property.
constexpr uint8 SpecRegistryProperty[] = {
	0x3E, 0x00,											// wLength 62
	0x04, 0x00,											// wDescriptorType 4, registry property
	0x04, 0x00,											// wPropertyDataType 4, REG_DWORD
	0x30, 0x00,											// wPropertyNameLength 48, the name with its terminator
	0x53, 0x00, 0x65, 0x00, 0x6C, 0x00, 0x65, 0x00,		// "Sele"
	0x63, 0x00, 0x74, 0x00, 0x69, 0x00, 0x76, 0x00,		// "ctiv"
	0x65, 0x00, 0x53, 0x00, 0x75, 0x00, 0x73, 0x00,		// "eSus"
	0x70, 0x00, 0x65, 0x00, 0x6E, 0x00, 0x64, 0x00,		// "pend"
	0x45, 0x00, 0x6E, 0x00, 0x61, 0x00, 0x62, 0x00,		// "Enab"
	0x6C, 0x00, 0x65, 0x00, 0x64, 0x00, 0x00, 0x00,		// "led" and the terminator
	0x04, 0x00,											// wPropertyDataLength 4
	0x01, 0x00, 0x00, 0x00								// PropertyData 0x00000001
};


// Hand-written from the specification's Example1: the 72-byte descriptor set, a set header then the registry property.
constexpr uint8 SpecExample1Set[] = {
	0x0A, 0x00,											// set header: wLength 10
	0x00, 0x00,											// MSOS20_SET_HEADER_DESCRIPTOR
	0x00, 0x00, 0x03, 0x06,								// dwWindowsVersion 0x06030000 (Windows 8.1)
	0x48, 0x00,											// wTotalLength 72
	0x3E, 0x00, 0x04, 0x00, 0x04, 0x00, 0x30, 0x00,		// registry property, as above
	0x53, 0x00, 0x65, 0x00, 0x6C, 0x00, 0x65, 0x00,
	0x63, 0x00, 0x74, 0x00, 0x69, 0x00, 0x76, 0x00,
	0x65, 0x00, 0x53, 0x00, 0x75, 0x00, 0x73, 0x00,
	0x70, 0x00, 0x65, 0x00, 0x6E, 0x00, 0x64, 0x00,
	0x45, 0x00, 0x6E, 0x00, 0x61, 0x00, 0x62, 0x00,
	0x6C, 0x00, 0x65, 0x00, 0x64, 0x00, 0x00, 0x00,
	0x04, 0x00, 0x01, 0x00, 0x00, 0x00
};


// Hand-written from the specification's Example1 layout: the 28-byte platform capability for that set (bLength 0x1C,
// bMS_VendorCode 0x01). The array is sized by its own bytes, not by a size taken from the specification text.
constexpr uint8 SpecPlatformCapability[] = {
	0x1C, 0x10, 0x05, 0x00,								// bLength 28, bDescriptorType 16, bDevCapabilityType 5, reserved
	0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C,		// MS OS 2.0 platform UUID {D8DD60DF-4589-4CC7-9CD2-659D9E648A9F}
	0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F,
	0x00, 0x00, 0x03, 0x06,								// dwWindowsVersion 0x06030000
	0x48, 0x00,											// wMSOSDescriptorSetTotalLength 72
	0x01,												// bMS_VendorCode
	0x00												// bAltEnumCode, no alternate enumeration
};


// Hand-written from the specification's subset layout: a function subset header (type 2) for interface 1 with wSubsetLength 28,
// then the WINUSB compatible ID.
constexpr uint8 SpecFunctionSubset[] = {
	0x08, 0x00, 0x02, 0x00, 0x01, 0x00, 0x1C, 0x00,		// wLength 8, type 2, bFirstInterface 1, reserved, wSubsetLength 28
	0x14, 0x00, 0x03, 0x00,								// compatible ID feature, 20 bytes
	0x57, 0x49, 0x4E, 0x55, 0x53, 0x42, 0x00, 0x00,		// CompatibleID "WINUSB"
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00		// SubCompatibleID, empty
};


// Hand-written from the specification's subset layout: a configuration subset (type 1) for configuration value 1 with
// wSubsetLength 36, holding the function subset above.
constexpr uint8 SpecConfigurationSubset[] = {
	0x08, 0x00, 0x01, 0x00, 0x01, 0x00, 0x24, 0x00,		// wLength 8, type 1, bConfigurationValue 1, reserved, wSubsetLength 36
	0x08, 0x00, 0x02, 0x00, 0x01, 0x00, 0x1C, 0x00,		// function subset for interface 1
	0x14, 0x00, 0x03, 0x00,								// compatible ID feature, 20 bytes
	0x57, 0x49, 0x4E, 0x55, 0x53, 0x42, 0x00, 0x00,		// CompatibleID "WINUSB"
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00		// SubCompatibleID, empty
};


// Hand-written: the descriptor set of the composite configuration, a 10-byte set header (dwWindowsVersion 0x06030000,
// wTotalLength 46) followed by the configuration subset above.
constexpr uint8 SpecCompositeSet[] = {
	0x0A, 0x00,											// set header: wLength 10
	0x00, 0x00,											// MSOS20_SET_HEADER_DESCRIPTOR
	0x00, 0x00, 0x03, 0x06,								// dwWindowsVersion 0x06030000 (Windows 8.1)
	0x2E, 0x00,											// wTotalLength 46
	0x08, 0x00, 0x01, 0x00, 0x01, 0x00, 0x24, 0x00,		// configuration subset, as above
	0x08, 0x00, 0x02, 0x00, 0x01, 0x00, 0x1C, 0x00,		// function subset, as above
	0x14, 0x00, 0x03, 0x00,
	0x57, 0x49, 0x4E, 0x55, 0x53, 0x42, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};


// Hand-written from the specification's registry property layout: DeviceInterfaceGUIDs as REG_MULTI_SZ (7). The data is
// BenchProtocol::InterfaceGuid as UTF-16LE with its terminator and one more terminator, 80 bytes. Total 132 bytes.
constexpr uint8 SpecGuidProperty[] = {
	0x84, 0x00, 0x04, 0x00, 0x07, 0x00, 0x2A, 0x00,		// wLength 132, type 4, wPropertyDataType 7 (REG_MULTI_SZ), name length 42
	0x44, 0x00, 0x65, 0x00, 0x76, 0x00, 0x69, 0x00,		// "Devi"
	0x63, 0x00, 0x65, 0x00, 0x49, 0x00, 0x6E, 0x00,		// "ceIn"
	0x74, 0x00, 0x65, 0x00, 0x72, 0x00, 0x66, 0x00,		// "terf"
	0x61, 0x00, 0x63, 0x00, 0x65, 0x00, 0x47, 0x00,		// "aceG"
	0x55, 0x00, 0x49, 0x00, 0x44, 0x00, 0x73, 0x00,		// "UIDs"
	0x00, 0x00,											// terminator of the name
	0x50, 0x00,											// wPropertyDataLength 80
	0x7B, 0x00, 0x39, 0x00, 0x43, 0x00, 0x46, 0x00,		// "{9CF"
	0x46, 0x00, 0x45, 0x00, 0x45, 0x00, 0x32, 0x00,		// "FEE2"
	0x31, 0x00, 0x2D, 0x00, 0x38, 0x00, 0x43, 0x00,		// "1-8C"
	0x33, 0x00, 0x31, 0x00, 0x2D, 0x00, 0x34, 0x00,		// "31-4"
	0x44, 0x00, 0x44, 0x00, 0x32, 0x00, 0x2D, 0x00,		// "DD2-"
	0x41, 0x00, 0x43, 0x00, 0x42, 0x00, 0x31, 0x00,		// "ACB1"
	0x2D, 0x00, 0x33, 0x00, 0x38, 0x00, 0x41, 0x00,		// "-38A"
	0x39, 0x00, 0x43, 0x00, 0x45, 0x00, 0x42, 0x00,		// "9CEB"
	0x30, 0x00, 0x35, 0x00, 0x44, 0x00, 0x41, 0x00,		// "05DA"
	0x34, 0x00, 0x7D, 0x00, 0x00, 0x00, 0x00, 0x00		// "4}" and the two terminators
};


// Hand-written: a composite configuration with an interface association (type 0x0B) over interfaces 0 and 1, wTotalLength 63.
// The device class for it is 0xEF, 0x02, 0x01.
constexpr uint8 SpecIadConfiguration[] = {
	0x09, 0x02, 0x3F, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,	// configuration: 63 bytes, 2 interfaces, value 1, bus powered, 100 mA
	0x08, 0x0B, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00,			// association: first interface 0, 2 interfaces, vendor function
	0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00,	// interface 0
	0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,				// endpoint 0x81: bulk IN
	0x07, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00,				// endpoint 0x01: bulk OUT
	0x09, 0x04, 0x01, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00,	// interface 1
	0x07, 0x05, 0x82, 0x02, 0x40, 0x00, 0x00,				// endpoint 0x82: bulk IN
	0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00				// endpoint 0x02: bulk OUT
};


constexpr uint8 VendorClass = static_cast<uint8>(USBClassCode::Vendor);


// Builder results. Each is a constant expression, so each is checked at compile time below and at run time in main().
constexpr std::array<uint8, 18> BuiltDevice = MakeUSBDeviceDescriptor(
	0x0201,
	0x00,
	0x00,
	0x00,
	static_cast<uint8>(USBDeviceAdapter::ControlMaxPacketSize),
	BenchProtocol::VendorId,
	BenchProtocol::ProductId,
	BenchProtocol::DeviceRelease,
	1,
	2,
	3,
	1
);


// The same device with the class of a composite that uses interface associations.
constexpr std::array<uint8, 18> IadDevice = MakeUSBDeviceDescriptor(
	0x0201,
	0xEF,
	0x02,
	0x01,
	static_cast<uint8>(USBDeviceAdapter::ControlMaxPacketSize),
	BenchProtocol::VendorId,
	BenchProtocol::ProductId,
	BenchProtocol::DeviceRelease,
	1,
	2,
	3,
	1
);


constexpr std::array<uint8, 32> BuiltConfiguration = MakeUSBConfigurationDescriptor(
	1,
	0,
	false,
	false,
	50,
	// The simulator fixture's own values (Descriptors.h), not the bench firmware's: the two identities are independent.
	MakeUSBInterfaceDescriptor(0, 0, VendorClass, 0, 0, 0),
	MakeUSBEndpointDescriptor(0x81, USBEndpointType::Bulk, 64, 0),
	MakeUSBEndpointDescriptor(0x01, USBEndpointType::Bulk, 64, 0)
);


constexpr std::array<uint8, 55> BuiltCompositeConfiguration = MakeUSBConfigurationDescriptor(
	1,
	0,
	false,
	false,
	50,
	MakeUSBInterfaceDescriptor(0, 0, VendorClass, 0, 0, 0),
	MakeUSBEndpointDescriptor(0x81, USBEndpointType::Bulk, 64, 0),
	MakeUSBEndpointDescriptor(0x01, USBEndpointType::Bulk, 64, 0),
	MakeUSBInterfaceDescriptor(1, 0, VendorClass, 0, 0, 0),
	MakeUSBEndpointDescriptor(0x82, USBEndpointType::Bulk, 64, 0),
	MakeUSBEndpointDescriptor(0x02, USBEndpointType::Bulk, 64, 0)
);


constexpr std::array<uint8, 8> BuiltAssociation = MakeUSBInterfaceAssociationDescriptor(0, 2, VendorClass, 0, 0, 0);


constexpr std::array<uint8, 63> BuiltIadConfiguration = MakeUSBConfigurationDescriptor(
	1,
	0,
	false,
	false,
	50,
	BuiltAssociation,
	MakeUSBInterfaceDescriptor(0, 0, VendorClass, 0, 0, 0),
	MakeUSBEndpointDescriptor(0x81, USBEndpointType::Bulk, 64, 0),
	MakeUSBEndpointDescriptor(0x01, USBEndpointType::Bulk, 64, 0),
	MakeUSBInterfaceDescriptor(1, 0, VendorClass, 0, 0, 0),
	MakeUSBEndpointDescriptor(0x82, USBEndpointType::Bulk, 64, 0),
	MakeUSBEndpointDescriptor(0x02, USBEndpointType::Bulk, 64, 0)
);


constexpr std::array<uint8, 4> BuiltLanguage = MakeUSBLanguageIdDescriptor(0x0409);
constexpr std::array<uint8, 8> BuiltManufacturer = MakeUSBStringDescriptor<"Sim">();
constexpr std::array<uint8, 10> BuiltProduct = MakeUSBStringDescriptor<"Bulk">();
constexpr std::array<uint8, 4> BuiltSerial = MakeUSBStringDescriptor<"1">();


constexpr std::array<std::span<const uint8>, 4> BuiltStringTable = {
	BuiltLanguage,
	BuiltManufacturer,
	BuiltProduct,
	BuiltSerial
};


// The MS OS 2.0 set that Descriptors.h spells out. The platform capability reads its header, so the bytes are the input.
constexpr std::array<uint8, 30> ExpectedMsOs20Set = ToArray(SimDescriptors::MsOs20Set);
constexpr std::array<uint8, 28> BuiltMsOs20Platform = MakeMsOs20PlatformCapability(ExpectedMsOs20Set, DescriptorVendorCode, NoAlternateEnumerationCode);
constexpr std::array<uint8, 40> BuiltBos = MakeUSBBosDescriptor(MakeUSBUsb2ExtensionCapability(false), BuiltMsOs20Platform);
constexpr USBVendorDescriptor BuiltMsOs20Request = MakeMsOs20VendorDescriptor(DescriptorVendorCode, SimDescriptors::MsOs20Set);


// The compatible ID feature, built from the specification's layout, and the registry property from the specification's Example1.
constexpr std::array<uint8, 20> BuiltCompatibleId = MakeMsOs20CompatibleId("WINUSB", "");
constexpr std::array<uint8, 62> BuiltRegistryProperty = MakeMsOs20RegistryProperty<"SelectiveSuspendEnabled">(
	USBMsOs20PropertyType::DwordLittleEndian,
	std::array<uint8, 4>{ 0x01, 0x00, 0x00, 0x00 }
);


// The subsets and the set built from them, and the DeviceInterfaceGUIDs property that the WinUSB backend reads.
constexpr std::array<uint8, 28> BuiltFunctionSubset = MakeMsOs20FunctionSubset(1, BuiltCompatibleId);
constexpr std::array<uint8, 36> BuiltConfigurationSubset = MakeMsOs20ConfigurationSubset(1, BuiltFunctionSubset);
constexpr std::array<uint8, 46> BuiltCompositeSet = MakeMsOs20DescriptorSet(0x06030000, BuiltConfigurationSubset);
// The device-level set of Descriptors.h: one compatible ID directly under the set header, for a single-function device.
constexpr std::array<uint8, 30> BuiltDeviceLevelMsOs20Set = MakeMsOs20DeviceLevelDescriptorSet(0x06030000, BuiltCompatibleId);
constexpr std::array<uint8, 132> BuiltGuidProperty = MakeMsOs20RegistryProperty<"DeviceInterfaceGUIDs">(
	USBMsOs20PropertyType::MultiString,
	MakeMsOs20MultiStringData(MakeUSBUtf16LENullTerminated<"{9CFFEE21-8C31-4DD2-ACB1-38A9CEB05DA4}">())
);


constexpr std::array<uint8, 72> SpecSetArray = ToArray(SpecExample1Set);
constexpr std::array<uint8, 28> BuiltSpecPlatform = MakeMsOs20PlatformCapability(SpecSetArray, SpecExampleVendorCode, NoAlternateEnumerationCode);


// The benchmark-sized device with a chosen bcdUSB and the rest as in BuiltDevice, for the BOS pairing rule.
constexpr std::array<uint8, 18> DeviceWithUsb(uint16 bcdUSB) {
	return MakeUSBDeviceDescriptor(
		bcdUSB,
		0x00,
		0x00,
		0x00,
		static_cast<uint8>(USBDeviceAdapter::ControlMaxPacketSize),
		BenchProtocol::VendorId,
		BenchProtocol::ProductId,
		BenchProtocol::DeviceRelease,
		1,
		2,
		3,
		1
	);
}


// Compile-time checks. Each compares a builder result with the hand-written bytes of Descriptors.h or the specification.
static_assert(BuiltDevice == ToArray(SimDescriptors::DeviceDescriptor), "device descriptor differs from Descriptors.h");
static_assert(BuiltConfiguration == ToArray(SimDescriptors::ConfigurationDescriptor), "configuration descriptor differs from Descriptors.h");
static_assert(BuiltCompositeConfiguration == ToArray(SimDescriptors::CompositeConfiguration), "composite configuration differs from Descriptors.h");
static_assert(BuiltLanguage == ToArray(SimDescriptors::StringLanguage), "language ID descriptor differs from Descriptors.h");
static_assert(BuiltManufacturer == ToArray(SimDescriptors::StringManufacturer), "manufacturer string differs from Descriptors.h");
static_assert(BuiltProduct == ToArray(SimDescriptors::StringProduct), "product string differs from Descriptors.h");
static_assert(BuiltSerial == ToArray(SimDescriptors::StringSerial), "serial string differs from Descriptors.h");
static_assert(BuiltMsOs20Platform == Slice<28>(SimDescriptors::BosDescriptor, 12), "MS OS 2.0 platform capability differs from Descriptors.h BOS");
static_assert(BuiltBos == ToArray(SimDescriptors::BosDescriptor), "BOS descriptor differs from Descriptors.h");
static_assert(BuiltCompatibleId == Slice<20>(SimDescriptors::MsOs20Set, 10), "compatible ID feature differs from Descriptors.h");
static_assert(BuiltMsOs20Request.request == SimDescriptors::VendorRequests[0].request, "MS OS 2.0 vendor request code differs");
static_assert(BuiltMsOs20Request.index == SimDescriptors::VendorRequests[0].index, "MS OS 2.0 vendor request index differs");
static_assert(USBDeviceClassMatchesConfiguration(BuiltDevice, BuiltConfiguration), "device class does not match a configuration without associations");
static_assert(USBDeviceClassMatchesConfiguration(BuiltDevice, BuiltCompositeConfiguration), "device class does not match the composite configuration");
static_assert(USBDeviceClassMatchesConfiguration(IadDevice, BuiltIadConfiguration), "device class 0xEF/0x02/0x01 does not match a configuration with associations");
static_assert(!USBDeviceClassMatchesConfiguration(BuiltDevice, BuiltIadConfiguration), "class 0x00 must not pass for a configuration with associations");
static_assert(!USBDeviceBosMatches(DeviceWithUsb(0x0200), BuiltBos), "a device with a BOS must announce bcdUSB 2.01 or later");
static_assert(USBDeviceBosMatches(DeviceWithUsb(0x0201), BuiltBos), "bcdUSB 2.01 is the lowest value that takes a BOS");
static_assert(USBDeviceBosMatches(BuiltDevice, BuiltBos), "the benchmark device (bcdUSB 2.01) takes its BOS");
static_assert(USBDeviceBosMatches(DeviceWithUsb(0x0200), std::span<const uint8>()), "a device without a BOS has no bcdUSB rule");
static_assert(BuiltDeviceLevelMsOs20Set == ToArray(SimDescriptors::MsOs20Set), "device-level MS OS 2.0 set differs from Descriptors.h");
static_assert(BuiltDeviceLevelMsOs20Set == ToArray(SpecTopLevelSet), "device-level MS OS 2.0 set differs from the specification layout");
static_assert(BuiltAssociation == Slice<8>(SpecIadConfiguration, 9), "interface association differs from the specification layout");
static_assert(BuiltIadConfiguration == ToArray(SpecIadConfiguration), "association configuration differs from the hand-written bytes");
static_assert(IadDevice == ToArray(SpecIadDevice), "composite device descriptor differs from the hand-written bytes");
static_assert(BuiltCompatibleId == Slice<20>(SpecCompatibleId, 0), "compatible ID feature differs from the specification layout");
static_assert(BuiltRegistryProperty == ToArray(SpecRegistryProperty), "registry property differs from the specification's Example1");
static_assert(BuiltSpecPlatform == ToArray(SpecPlatformCapability), "platform capability differs from the specification's Example1");
static_assert(BuiltFunctionSubset == ToArray(SpecFunctionSubset), "function subset differs from the specification layout");
static_assert(BuiltConfigurationSubset == ToArray(SpecConfigurationSubset), "configuration subset differs from the specification layout");
static_assert(BuiltCompositeSet == ToArray(SpecCompositeSet), "composite descriptor set differs from the specification layout");
static_assert(BuiltGuidProperty == ToArray(SpecGuidProperty), "DeviceInterfaceGUIDs property differs from the specification layout");
static_assert(std::string_view(BenchProtocol::InterfaceGuid) == std::string_view("{9CFFEE21-8C31-4DD2-ACB1-38A9CEB05DA4}"), "the GUID literal must equal BenchProtocol::InterfaceGuid");


}


int main() {
	ExpectBytes("device descriptor", BuiltDevice, SimDescriptors::DeviceDescriptor);
	ExpectBytes("configuration descriptor", BuiltConfiguration, SimDescriptors::ConfigurationDescriptor);
	ExpectBytes("composite configuration", BuiltCompositeConfiguration, SimDescriptors::CompositeConfiguration);
	ExpectBytes("language ID descriptor", BuiltStringTable[0], SimDescriptors::StringTable[0]);
	ExpectBytes("manufacturer string", BuiltStringTable[1], SimDescriptors::StringTable[1]);
	ExpectBytes("product string", BuiltStringTable[2], SimDescriptors::StringTable[2]);
	ExpectBytes("serial string", BuiltStringTable[3], SimDescriptors::StringTable[3]);
	ExpectBytes("BOS descriptor", BuiltBos, SimDescriptors::BosDescriptor);
	ExpectBytes("MS OS 2.0 platform capability", BuiltMsOs20Platform, std::span<const uint8>(SimDescriptors::BosDescriptor).subspan(12, 28));
	ExpectBytes("MS OS 2.0 set as served", SimDescriptors::MsOs20Set, SpecTopLevelSet);

	ExpectBytes("association configuration", BuiltIadConfiguration, SpecIadConfiguration);
	ExpectBytes("composite device descriptor", IadDevice, SpecIadDevice);

	// The vendor request: the code and index must be the ones Descriptors.h declares, and the bytes must be the set's own storage.
	// The span is compared by its data pointer and size, because comparing its bytes with the same set could never fail.
	SIM_EXPECT_EQ(BuiltMsOs20Request.request, DescriptorVendorCode);
	SIM_EXPECT_EQ(BuiltMsOs20Request.index, SimDescriptors::VendorRequests[0].index);
	SIM_EXPECT(BuiltMsOs20Request.bytes.data() == SimDescriptors::MsOs20Set);
	SIM_EXPECT_EQ(BuiltMsOs20Request.bytes.size(), 30);

	ExpectBytes("specification compatible ID (WINUSB)", BuiltCompatibleId, SpecCompatibleId);
	ExpectBytes("specification registry property (Example1)", BuiltRegistryProperty, SpecRegistryProperty);
	ExpectBytes("specification platform capability (Example1)", BuiltSpecPlatform, SpecPlatformCapability);
	ExpectBytes("MS OS 2.0 function subset (WINUSB, interface 1)", BuiltFunctionSubset, SpecFunctionSubset);
	ExpectBytes("MS OS 2.0 configuration subset", BuiltConfigurationSubset, SpecConfigurationSubset);
	ExpectBytes("MS OS 2.0 composite descriptor set", BuiltCompositeSet, SpecCompositeSet);
	ExpectBytes("DeviceInterfaceGUIDs registry property", BuiltGuidProperty, SpecGuidProperty);

	// The device descriptor claims bcdUSB 2.01, the lowest version that USBDeviceSupportsBos() accepts for a BOS.
	// The rule is evaluated as a constant, because a run-time call of a builder helper would not link: USBDescriptorRejected
	// is declared and never defined, and a run-time call keeps the reference (see USBDescriptorUtility.h).
	constexpr bool bosRuleHolds = USBDeviceSupportsBos(BuiltDevice);
	SIM_EXPECT(bosRuleHolds);

	// The device-level MS OS 2.0 set is built by the builder and compared with the bytes Descriptors.h serves, and with the
	// specification layout.
	ExpectBytes("MS OS 2.0 device-level set built by the builder", BuiltDeviceLevelMsOs20Set, SimDescriptors::MsOs20Set);
	ExpectBytes("MS OS 2.0 device-level set (specification layout)", BuiltDeviceLevelMsOs20Set, SpecTopLevelSet);

	return SimTest::Finish("TestDescriptorBuilders");
}
