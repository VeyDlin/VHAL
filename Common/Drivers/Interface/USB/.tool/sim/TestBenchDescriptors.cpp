#include <VHAL.h>
#include <DemoUSBBench/Descriptors/BenchDescriptors.h>
#include <Drivers/Interface/USB/Descriptor/MsOs20/USBMsOs20DescriptorSet.h>
#include <Drivers/Interface/USB/Descriptor/MsOs20/USBMsOs20PlatformCapability.h>
#include <Drivers/Interface/USB/Descriptor/USBBosDescriptor.h>
#include <Drivers/Interface/USB/Descriptor/USBConfigurationDescriptor.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorComposition.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorSet.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorString.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorUtility.h>
#include <Drivers/Interface/USB/Descriptor/USBDeviceDescriptor.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <Drivers/Interface/USB/USBProtocol.h>
#include <BenchProtocol.h>
#include "SimAssert.h"
#include "SimUSBDeviceAdapter.h"
#include "TestFunction.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>


// The fluent benchmark descriptors against the positional ones they replace. The reference below is the positional
// BenchDescriptors.h as it was before the fluent builder, copied unchanged except for the namespace and the struct name.
// Every byte must match except one known difference: the serial number string. The builder reserves a 2-byte placeholder, and the
// real string comes from SetSerialNumber(). The device, configuration, BOS, the other strings, the MS OS 2.0 set and the vendor
// request are compared byte for byte.


namespace Reference {
// ---- Copied from .demo/STM32G4/Application/DemoUSBBench/Descriptors/BenchDescriptors.h before the fluent builder. ----
// Only the struct name differs, so the reference can live in this test next to the fluent set.

// Encodes text of a known length as UTF-16LE with a 16-bit NUL after it. The GUID comes as a const char* constant, not as a
// string literal class parameter, so the string builders cannot take it; this does the same work at compile time.
template<std::size_t Size>
constexpr std::array<uint8, Size> MakeUtf16Terminated(const char* text, std::size_t length) {
	std::array<uint8, Size> bytes{};
	USBEncodeUtf16(text, length, bytes, 0);
	return bytes;
}


// The descriptors of the benchmark device. They are all constexpr, so they live in flash, and every span refers to static
// storage, as USBDevice requires. The identities come from BenchProtocol.h; the library itself has none.
struct ReferenceBenchDescriptors {
	// Indexes of the strings the device descriptor and the interface name. strings[0] is the language ID.
	static constexpr uint8 ManufacturerIndex = 1;
	static constexpr uint8 ProductIndex = 2;
	static constexpr uint8 SerialIndex = 3;
	static constexpr uint8 InterfaceIndex = 4;
	// bcdUSB 2.10 so the host asks for the BOS (it is requested from 2.01 on). Class 0x00: no interface association.
	static constexpr std::array<uint8, 18> Device = MakeUSBDeviceDescriptor(
		0x0210,
		0x00,
		0x00,
		0x00,
		USBDeviceAdapter::ControlMaxPacketSize,
		BenchProtocol::VendorId,
		BenchProtocol::ProductId,
		BenchProtocol::DeviceRelease,
		ManufacturerIndex,
		ProductIndex,
		SerialIndex,
		1
	);
	static constexpr std::array<uint8, 4> LanguageString = MakeUSBLanguageIdDescriptor(0x0409);
	static constexpr std::array<uint8, 10> ManufacturerString = MakeUSBStringDescriptor<"VHAL">();
	static constexpr std::array<uint8, 20> ProductString = MakeUSBStringDescriptor<"USB Bench">();
	static constexpr std::array<uint8, 10> SerialString = MakeUSBStringDescriptor<"0001">();
	static constexpr std::array<uint8, 24> InterfaceString = MakeUSBStringDescriptor<"Vendor bulk">();
	static constexpr std::array<std::span<const uint8>, 5> Strings{
		std::span<const uint8>(LanguageString),
		std::span<const uint8>(ManufacturerString),
		std::span<const uint8>(ProductString),
		std::span<const uint8>(SerialString),
		std::span<const uint8>(InterfaceString)
	};
	// One vendor interface (class 0xFF) with the two bulk endpoints. No interface association, so the device class above
	// is 0x00, as USBDeviceClassMatchesConfiguration() checks.
	static constexpr std::array<uint8, 9> InterfaceDescriptor = MakeUSBInterfaceDescriptor(
		BenchProtocol::InterfaceNumber,
		0,
		static_cast<uint8>(USBClassCode::Vendor),
		0,
		0,
		InterfaceIndex
	);
	static constexpr std::array<uint8, 7> BulkInDescriptor = MakeUSBEndpointDescriptor(
		BenchProtocol::BulkInAddress,
		USBEndpointType::Bulk,
		BenchProtocol::BulkPacketSize,
		0
	);
	static constexpr std::array<uint8, 7> BulkOutDescriptor = MakeUSBEndpointDescriptor(
		BenchProtocol::BulkOutAddress,
		USBEndpointType::Bulk,
		BenchProtocol::BulkPacketSize,
		0
	);
	// Bus-powered, 100 mA (maxPower is in units of 2 mA).
	static constexpr std::array<uint8, 32> Configuration = MakeUSBConfigurationDescriptor(
		1,
		0,
		false,
		false,
		50,
		InterfaceDescriptor,
		BulkInDescriptor,
		BulkOutDescriptor
	);


	// The Microsoft OS 2.0 descriptor set (Windows only; other hosts ignore it). The compatible ID selects WinUSB, and the
	// DeviceInterfaceGUIDs property names the interface GUID the WinUSB backend opens the device with.
	static constexpr std::array<uint8, 20> CompatibleId = MakeMsOs20CompatibleId("WINUSB", "");
	static constexpr std::size_t GuidLength = std::string_view(BenchProtocol::InterfaceGuid).size();
	static constexpr std::size_t GuidUnits = USBCountUtf16Units(BenchProtocol::InterfaceGuid, GuidLength).units;


	// The GUID as one UTF-16LE string with its terminator (REG_MULTI_SZ takes such strings, then one more terminator).
	static constexpr std::array<uint8, 2 * (GuidUnits + 1)> InterfaceGuidUtf16 =
		MakeUtf16Terminated<2 * (GuidUnits + 1)>(BenchProtocol::InterfaceGuid, GuidLength);
	static constexpr std::array<uint8, 2 * (GuidUnits + 1) + 2> InterfaceGuids = MakeMsOs20MultiStringData(InterfaceGuidUtf16);
	// 10 bytes of header, 42 for the name "DeviceInterfaceGUIDs" (20 characters) with its terminator, then 80 bytes of data.
	static constexpr std::array<uint8, 132> InterfaceGuidProperty = MakeMsOs20RegistryProperty<"DeviceInterfaceGUIDs">(
		USBMsOs20PropertyType::MultiString,
		InterfaceGuids
	);
	// The device is not composite (one interface, device class 0x00), so the features stand directly under the set header.
	// 0x06030000 is Windows 8.1, the version this set is written for. 10 + 20 + 132 bytes.
	static constexpr std::array<uint8, 162> MsOsSet = MakeMsOs20DeviceLevelDescriptorSet(
		0x06030000,
		CompatibleId,
		InterfaceGuidProperty
	);
	static constexpr std::array<uint8, 28> MsOsPlatform = MakeMsOs20PlatformCapability(
		MsOsSet,
		BenchProtocol::MsOs20VendorCode,
		0
	);
	static constexpr std::array<uint8, 33> Bos = MakeUSBBosDescriptor(MsOsPlatform);
	static constexpr std::array<USBVendorDescriptor, 1> VendorRequests{
		MakeMsOs20VendorDescriptor(BenchProtocol::MsOs20VendorCode, MsOsSet)
	};
	static constexpr USBDescriptorSet DescriptorSet{ Device, Configuration, Strings, Bos, VendorRequests };


	// Offsets as in the positional header, so the reference is checked the same way it was before.
	static_assert(USBDescriptorRead16(Device, 8) == BenchProtocol::VendorId, "idVendor must be BenchProtocol::VendorId");
	static_assert(USBDescriptorRead16(Device, 10) == BenchProtocol::ProductId, "idProduct must be BenchProtocol::ProductId");
	static_assert(USBDescriptorRead16(Device, 12) == BenchProtocol::DeviceRelease, "bcdDevice must be BenchProtocol::DeviceRelease");
	static_assert(Configuration[11] == BenchProtocol::InterfaceNumber, "the vendor interface must be BenchProtocol::InterfaceNumber");
	static_assert(Configuration[20] == BenchProtocol::BulkInAddress, "the bulk IN endpoint must be BenchProtocol::BulkInAddress");
	static_assert(Configuration[27] == BenchProtocol::BulkOutAddress, "the bulk OUT endpoint must be BenchProtocol::BulkOutAddress");
	static_assert(USBDescriptorRead16(Configuration, 22) == BenchProtocol::BulkPacketSize, "bulk IN packet size must be BenchProtocol::BulkPacketSize");
	static_assert(USBDescriptorRead16(Configuration, 29) == BenchProtocol::BulkPacketSize, "bulk OUT packet size must be BenchProtocol::BulkPacketSize");
	static_assert(Bos[31] == BenchProtocol::MsOs20VendorCode, "the BOS must carry BenchProtocol::MsOs20VendorCode");
	static_assert(VendorRequests[0].request == BenchProtocol::MsOs20VendorCode, "the vendor request must be BenchProtocol::MsOs20VendorCode");
	static_assert(VendorRequests[0].index == USBMsOs20DescriptorIndex, "the MS OS 2.0 descriptor is requested with wIndex 7");
};
// ---- End of the copied reference. ----
}


namespace {


// Compares two byte sequences. A mismatch prints the first differing offset and both lengths.
void ExpectBytes(const char* name, std::span<const uint8> built, std::span<const uint8> expected) {
	bool same = built.size() == expected.size() && std::equal(built.begin(), built.end(), expected.begin());
	if (same) {
		return;
	}

	std::size_t firstDifference = 0;
	while (firstDifference < built.size() && firstDifference < expected.size() && built[firstDifference] == expected[firstDifference]) {
		firstDifference++;
	}

	std::fprintf(stderr, "%s differs: built %zu bytes, expected %zu, first difference at offset %zu\n",
		name, built.size(), expected.size(), firstDifference);
	SimTest::Expect(false, name, __FILE__, __LINE__);
}


// The serial string the fluent set reserves is a 2-byte placeholder, and the positional one was "0001" (10 bytes).
void CheckStrings() {
	const std::array<std::span<const uint8>, 5>& table = BenchUsb::StringTable;
	SIM_EXPECT_EQ(table.size(), Reference::ReferenceBenchDescriptors::Strings.size());
	ExpectBytes("language", table[0], Reference::ReferenceBenchDescriptors::LanguageString);
	ExpectBytes("manufacturer", table[1], Reference::ReferenceBenchDescriptors::ManufacturerString);
	ExpectBytes("product", table[2], Reference::ReferenceBenchDescriptors::ProductString);
	ExpectBytes("interface name", table[4], Reference::ReferenceBenchDescriptors::InterfaceString);

	// The placeholder: bLength 2, bDescriptorType 3, no text. SetSerialNumber() replaces it at run time.
	const uint8 placeholder[2] = { 0x02, 0x03 };
	ExpectBytes("serial placeholder", table[3], placeholder);
}


void CheckBytes() {
	ExpectBytes("device", BenchUsb::Device, Reference::ReferenceBenchDescriptors::Device);
	ExpectBytes("configuration", BenchUsb::Configuration, Reference::ReferenceBenchDescriptors::Configuration);
	ExpectBytes("MS OS 2.0 set", BenchUsb::MsOs20Set, Reference::ReferenceBenchDescriptors::MsOsSet);
	ExpectBytes("BOS", BenchUsb::Bos, Reference::ReferenceBenchDescriptors::Bos);
	ExpectBytes("vendor request bytes", BenchUsb::VendorRequests[0].bytes, Reference::ReferenceBenchDescriptors::MsOsSet);
	SIM_EXPECT_EQ(BenchUsb::VendorRequests[0].request, BenchProtocol::MsOs20VendorCode);
	SIM_EXPECT_EQ(BenchUsb::VendorRequests[0].index, USBMsOs20DescriptorIndex);
}


// The set the device gets: the same bytes as the parts, a run-time serial number, and the device starts only after the serial number
// is given. The vendor function uses the bench endpoints 0x81 and 0x02.
void CheckSetAndStart() {
	const USBDescriptorSet& set = BenchUsb::Set;
	SIM_EXPECT(set.serialNumberAtRuntime);
	ExpectBytes("set device", set.device, BenchUsb::Device);
	ExpectBytes("set configuration", set.configuration, BenchUsb::Configuration);
	SIM_EXPECT_EQ(set.strings.size(), BenchUsb::StringTable.size());

	SimUSBDeviceAdapter sim;
	const uint8 interfaces[] = { BenchProtocol::InterfaceNumber };
	const uint8 endpoints[] = { BenchProtocol::BulkInAddress, BenchProtocol::BulkOutAddress };
	TestFunction function(interfaces, endpoints, BenchProtocol::BulkInAddress, BenchProtocol::BulkOutAddress);
	std::array<USBFunction*, 1> functions{ &function };
	USBDevice device(sim, set, functions);

	USBDeviceAdapter::Parameters parameters;
	SIM_EXPECT_EQ(device.Start(parameters), ResultStatus::invalidParameter);

	const uint8 serial[14] = { 0x0E, 0x03, 0x31, 0x00, 0x32, 0x00, 0x41, 0x00, 0x42, 0x00, 0x43, 0x00, 0x44, 0x00 };
	SIM_EXPECT_EQ(device.SetSerialNumber(serial), ResultStatus::ok);
	SIM_EXPECT_EQ(device.Start(parameters), ResultStatus::ok);
}


}


int main() {
	CheckBytes();
	CheckStrings();
	CheckSetAndStart();
	return SimTest::Finish("TestBenchDescriptors");
}
