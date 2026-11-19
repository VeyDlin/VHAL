#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string_view>
#include <VHAL.h>
#include <Drivers/Interface/USB/Class/Vendor/USBVendorBuilder.h>
#include <Drivers/Interface/USB/Class/Vendor/VendorBulkFunction.h>
#include <Drivers/Interface/USB/Class/Vendor/VendorBulkStreamFunction.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBConfigurationBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include <Drivers/Interface/USB/Descriptor/MsOs20/USBMsOs20DescriptorSet.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorString.h>
#include "Descriptors.h"
#include "SimAssert.h"

namespace {


// Copies a hand-written C array into a std::array, so a comparison can be a static_assert.
template<std::size_t Size>
constexpr std::array<uint8, Size> ToArray(const uint8 (&bytes)[Size]) {
	std::array<uint8, Size> copy{};
	for (std::size_t index = 0; index < Size; index++) {
		copy[index] = bytes[index];
	}

	return copy;
}


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


// The fixture device of Descriptors.h, written with the fluent builder: one vendor bulk interface with a bulk IN 0x81 and OUT
// 0x01, WinUSB through the device-level MS OS 2.0 set, the identities of the fixture, and the USB 2.0 extension in the BOS (the
// option is off by default, and Descriptors.h has the extension).
using FixtureUsb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.UsbVersion(0x0201)
		.Usb2Extension()
		.Manufacturer("Sim")
		.Product("Bulk")
		.SerialNumber("1")
		.MsOs20VendorCode(0x42)
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBVendorBuilder()
				.BulkIn(0x81)
				.BulkOut(0x01)
				.WinUsb()));
}>;


// The same device without a BOS or a vendor request: two vendor interfaces (0x81/0x01 and 0x82/0x02), no association.
using FixtureCompositeUsb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.UsbVersion(0x0201)
		.Manufacturer("Sim")
		.Product("Bulk")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBVendorBuilder().BulkIn(0x81).BulkOut(0x01))
			.Function(USBVendorBuilder().BulkIn(0x82).BulkOut(0x02)));
}>;


// Auto numbering: no addresses given, so the IN endpoint takes number 1 and the OUT endpoint the next number unused in either
// direction, which is 2.
using AutoUsb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Auto")
		.Product("Auto")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBVendorBuilder())
			.Function(USBVendorBuilder().Name("Vendor")));
}>;


// A class builder of two interfaces, written against the concept only: it is not a library class. Its interfaces carry the
// string "Pair", the same text as the second function's name, so the strings are shared.
class PairFunction {
public:
	constexpr std::size_t InterfaceCount() const {
		return 2;
	}


	constexpr std::size_t EndpointCount() const {
		return 4;
	}


	constexpr USBEndpointRequest Endpoint(std::size_t index) const {
		USBEndpointRequest request;
		request.type = USBEndpointType::Bulk;
		request.direction = index % 2 == 0 ? USBDirection::In : USBDirection::Out;
		request.maxPacketSize = 64;
		return request;
	}


	constexpr std::size_t StringCount() const {
		return 1;
	}


	constexpr std::string_view String(std::size_t) const {
		return "Pair";
	}


	constexpr uint8 FunctionClass() const {
		return 0xFF;
	}


	constexpr uint8 FunctionSubClass() const {
		return 0x00;
	}


	constexpr uint8 FunctionProtocol() const {
		return 0x00;
	}


	constexpr bool HasWinUsb() const {
		return false;
	}


	constexpr std::string_view WinUsbGuid() const {
		return std::string_view();
	}


	constexpr void Emit(USBFunctionEmitter& emitter) const {
		emitter.Interface(0, 0xFF, 0x00, 0x00, 0);
		emitter.Endpoint(0);
		emitter.Endpoint(1);
		emitter.Interface(1, 0xFF, 0x00, 0x00, USBNoString);
		emitter.Endpoint(2);
		emitter.Endpoint(3);
	}
};


// A single interface class with one interrupt IN endpoint, 16-byte packets and interval 4 ms, as a HID function would have it.
class InterruptFunction {
public:
	constexpr std::size_t InterfaceCount() const {
		return 1;
	}


	constexpr std::size_t EndpointCount() const {
		return 1;
	}


	constexpr USBEndpointRequest Endpoint(std::size_t) const {
		USBEndpointRequest request;
		request.type = USBEndpointType::Interrupt;
		request.direction = USBDirection::In;
		request.address = 0x83;
		request.maxPacketSize = 16;
		request.interval = 4;
		return request;
	}


	constexpr std::size_t StringCount() const {
		return 0;
	}


	constexpr std::string_view String(std::size_t) const {
		return std::string_view();
	}


	constexpr uint8 FunctionClass() const {
		return 0x03;
	}


	constexpr uint8 FunctionSubClass() const {
		return 0x00;
	}


	constexpr uint8 FunctionProtocol() const {
		return 0x00;
	}


	constexpr bool HasWinUsb() const {
		return false;
	}


	constexpr std::string_view WinUsbGuid() const {
		return std::string_view();
	}


	constexpr void Emit(USBFunctionEmitter& emitter) const {
		emitter.Interface(0, 0x03, 0x00, 0x00, USBNoString);
		emitter.Endpoint(0);
	}
};


// A composite with an interface association (the pair, interfaces 0 and 1) and a named vendor interface that shares the string.
// Manufacturer comes first and Product and the serial number after the configuration, so "Pair" is string 2.
using AssociatedUsb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("VHAL")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(PairFunction())
			.Function(USBVendorBuilder().Name("Pair").BulkIn(0x85).BulkOut(0x05)))
		.Product("Bulk")
		.SerialNumber("1");
}>;


// A device with a single interrupt function, self-powered with remote wakeup: the attribute bits 6 and 5 are set.
using InterruptUsb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Int")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.SelfPowered()
			.RemoteWakeup()
			.Function(InterruptFunction()));
}>;


// Two WinUSB vendor functions in one configuration: MS OS 2.0 gets one function subset per function.
using TwoWinUsbUsb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.UsbVersion(0x0201)
		.Manufacturer("Sim")
		.Product("Bulk")
		.SerialNumber("1")
		.MsOs20VendorCode(0x42)
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBVendorBuilder().BulkIn(0x81).BulkOut(0x01).WinUsb())
			.Function(USBVendorBuilder().BulkIn(0x82).BulkOut(0x02).WinUsb()));
}>;


// Two WinUSB vendor functions, each with its own interface GUID: each function subset holds the GUID property, so its length counts
// the property, and the BOS carries the length of the whole set.
using TwoWinUsbGuidUsb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.UsbVersion(0x0201)
		.Manufacturer("Sim")
		.Product("Bulk")
		.SerialNumber("1")
		.MsOs20VendorCode(0x42)
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBVendorBuilder().BulkIn(0x81).BulkOut(0x01).WinUsb("{9CFFEE21-8C31-4DD2-ACB1-38A9CEB05DA4}"))
			.Function(USBVendorBuilder().BulkIn(0x82).BulkOut(0x02).WinUsb("{5F2B3F4E-0F3A-4B2C-9D1E-7A6B5C4D3E2F}")));
}>;


// A device-level set with a DeviceInterfaceGUIDs property: the GUID property is taken from the existing registry builder.
using GuidUsb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.UsbVersion(0x0201)
		.Manufacturer("Sim")
		.Product("Bulk")
		.SerialNumber("1")
		.MsOs20VendorCode(0x42)
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBVendorBuilder().BulkIn(0x81).BulkOut(0x01).WinUsb("{9CFFEE21-8C31-4DD2-ACB1-38A9CEB05DA4}")));
}>;


// The language, non-ASCII text and a code point above U+FFFF (a surrogate pair in UTF-16): 0x0407 (German), "é" and "😀".
using StringsUsb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Language(0x0407)
		.Manufacturer("\xC3\xA9")
		.Product("\xF0\x9F\x98\x80")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBVendorBuilder().BulkIn(0x81).BulkOut(0x01)));
}>;


// A BOS with the USB 2.0 extension alone: no MS OS 2.0 set and no vendor request, and bcdUSB 0x0210 by default.
using Usb2OnlyUsb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Usb2Extension()
		.Manufacturer("Sim")
		.Product("Bulk")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBVendorBuilder().BulkIn(0x81).BulkOut(0x01)));
}>;


// A device with a runtime serial number: the placeholder string is reserved at index 3.
using RuntimeSerialUsb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Bulk")
		.SerialNumberAtRuntime()
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBVendorBuilder().BulkIn(0x81).BulkOut(0x01)));
}>;


constexpr std::array<uint8, 18> FixtureDevice = ToArray(SimDescriptors::DeviceDescriptor);
constexpr std::array<uint8, 32> FixtureConfiguration = ToArray(SimDescriptors::ConfigurationDescriptor);
constexpr std::array<uint8, 55> FixtureComposite = ToArray(SimDescriptors::CompositeConfiguration);
constexpr std::array<uint8, 40> FixtureBos = ToArray(SimDescriptors::BosDescriptor);
constexpr std::array<uint8, 30> FixtureMsOs20Set = ToArray(SimDescriptors::MsOs20Set);


// The rule the fixture device must satisfy, checked at compile time on the builder's own arrays.
static_assert(FixtureUsb::Device == FixtureDevice, "device descriptor differs from Descriptors.h");
static_assert(FixtureUsb::Configuration == FixtureConfiguration, "configuration differs from Descriptors.h");
static_assert(FixtureUsb::Bos == FixtureBos, "BOS differs from Descriptors.h");
static_assert(FixtureUsb::MsOs20Set == FixtureMsOs20Set, "MS OS 2.0 set differs from Descriptors.h");
static_assert(FixtureCompositeUsb::Configuration == FixtureComposite, "composite configuration differs from Descriptors.h");
static_assert(FixtureUsb::Set.bos.size() == 40, "the fixture device has a BOS with the USB 2.0 extension");


// Hand-written MS OS 2.0 set of two function subsets, one per WinUSB function, at interfaces 0 and 1 (Microsoft OS 2.0
// Descriptors Specification, subset layout). wTotalLength 66.
constexpr uint8 TwoSubsetSet[] = {
	0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x06, 0x42, 0x00,		// set header, wTotalLength 66
	0x08, 0x00, 0x02, 0x00, 0x00, 0x00, 0x1C, 0x00,					// function subset, first interface 0, 28 bytes
	0x14, 0x00, 0x03, 0x00, 0x57, 0x49, 0x4E, 0x55, 0x53, 0x42, 0x00, 0x00,	// compatible ID WINUSB
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x08, 0x00, 0x02, 0x00, 0x01, 0x00, 0x1C, 0x00,					// function subset, first interface 1, 28 bytes
	0x14, 0x00, 0x03, 0x00, 0x57, 0x49, 0x4E, 0x55, 0x53, 0x42, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};


constexpr std::array<uint8, 66> TwoSubsetArray = ToArray(TwoSubsetSet);
static_assert(TwoWinUsbUsb::MsOs20Set == TwoSubsetArray, "two WinUSB functions give two function subsets");


// Expected device-level set with the GUID property: the header, the compatible ID and the registry property built by the
// existing registry builder (the reference the fluent builder must match).
constexpr std::array<uint8, 10> GuidSetHeader = {
	0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x06, 0xA2, 0x00
};


constexpr std::array<uint8, 20> GuidCompatibleId = MakeMsOs20CompatibleId("WINUSB", "");
constexpr std::array<uint8, 132> GuidProperty = MakeMsOs20RegistryProperty<USBText{"DeviceInterfaceGUIDs"}>(
	USBMsOs20PropertyType::MultiString,
	MakeMsOs20MultiStringData(MakeUSBUtf16LENullTerminated<USBText{"{9CFFEE21-8C31-4DD2-ACB1-38A9CEB05DA4}"}>())
);
constexpr std::array<uint8, 132> GuidPropertyTwo = MakeMsOs20RegistryProperty<USBText{"DeviceInterfaceGUIDs"}>(
	USBMsOs20PropertyType::MultiString,
	MakeMsOs20MultiStringData(MakeUSBUtf16LENullTerminated<USBText{"{5F2B3F4E-0F3A-4B2C-9D1E-7A6B5C4D3E2F}"}>())
);


// Two function subsets with GUIDs. The set is 10 + 2 * 160 = 330 bytes, and each subset is 8 + 20 + 132 = 160 bytes. The headers are
// written out here; the compatible ID and the GUID properties come from the registry builder, which the device-level case checks.
constexpr uint8 TwoGuidSetHeader[] = {
	0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x06, 0x4A, 0x01		// set header, wTotalLength 330
};


constexpr uint8 TwoGuidSubsetHeader0[] = {
	0x08, 0x00, 0x02, 0x00, 0x00, 0x00, 0xA0, 0x00					// function subset, first interface 0, 160 bytes
};


constexpr uint8 TwoGuidSubsetHeader1[] = {
	0x08, 0x00, 0x02, 0x00, 0x01, 0x00, 0xA0, 0x00					// function subset, first interface 1, 160 bytes
};


// The configuration of the associated pair: a 3-interface configuration of 86 bytes, written out by hand. The association covers
// interfaces 0 and 1 (class 0xFF), interface 2 is the named vendor function. "Pair" is string 2 on the interfaces 0 and 2, and
// interface 1 has no name.
constexpr uint8 AssociatedConfiguration[] = {
	0x09, 0x02, 0x56, 0x00, 0x03, 0x01, 0x00, 0x80, 0x32,			// configuration, 3 interfaces, 100 mA
	0x08, 0x0B, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00,					// interface association: 0 and 1, class 0xFF, no name
	0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x02,			// interface 0, 2 endpoints, iInterface "Pair"
	0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,						// bulk IN 0x81
	0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00,						// bulk OUT 0x02, the next number unused in either direction
	0x09, 0x04, 0x01, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00,			// interface 1, 2 endpoints, no name
	0x07, 0x05, 0x83, 0x02, 0x40, 0x00, 0x00,						// bulk IN 0x83
	0x07, 0x05, 0x04, 0x02, 0x40, 0x00, 0x00,						// bulk OUT 0x04
	0x09, 0x04, 0x02, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x02,			// interface 2 (vendor), 2 endpoints, iInterface "Pair"
	0x07, 0x05, 0x85, 0x02, 0x40, 0x00, 0x00,						// bulk IN 0x85
	0x07, 0x05, 0x05, 0x02, 0x40, 0x00, 0x00						// bulk OUT 0x05
};


constexpr std::array<uint8, 86> AssociatedConfigurationArray = ToArray(AssociatedConfiguration);


// The configuration of the interrupt function: self-powered (bit 6) and remote wakeup (bit 5), 25 bytes.
constexpr uint8 InterruptConfiguration[] = {
	0x09, 0x02, 0x19, 0x00, 0x01, 0x01, 0x00, 0xE0, 0x32,			// configuration, self-powered and remote wakeup, 100 mA
	0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,			// interface 0, one endpoint, class 0x03
	0x07, 0x05, 0x83, 0x03, 0x10, 0x00, 0x04						// interrupt IN 0x83, 16 bytes, interval 4
};


constexpr std::array<uint8, 25> InterruptConfigurationArray = ToArray(InterruptConfiguration);


// The BOS of the USB 2.0 extension alone: header of 12 bytes, one capability, LPM off.
constexpr uint8 Usb2OnlyBos[] = {
	0x05, 0x0F, 0x0C, 0x00, 0x01,
	0x07, 0x10, 0x02, 0x00, 0x00, 0x00, 0x00
};


// String descriptor 0 for 0x0407, and the descriptors of "é" (U+00E9, one unit) and "😀" (U+1F600, a surrogate pair D83D DE00).
constexpr uint8 GermanLanguage[] = { 0x04, 0x03, 0x07, 0x04 };
constexpr uint8 AccentString[] = { 0x04, 0x03, 0xE9, 0x00 };
constexpr uint8 EmojiString[] = { 0x06, 0x03, 0x3D, 0xD8, 0x00, 0xDE };


// A runtime serial number: the placeholder string (an empty descriptor) answers index 3 and the flag is set.
void CheckRuntimeSerial() {
	SIM_EXPECT(RuntimeSerialUsb::Set.serialNumberAtRuntime);
	SIM_EXPECT_EQ(RuntimeSerialUsb::Set.strings.size(), 4);
	SIM_EXPECT_EQ(RuntimeSerialUsb::Set.strings[3].size(), 2);
	SIM_EXPECT_EQ(RuntimeSerialUsb::Device[16], 3);
}


}


int main() {
	ExpectBytes("device descriptor", FixtureUsb::Device, SimDescriptors::DeviceDescriptor);
	ExpectBytes("configuration descriptor", FixtureUsb::Configuration, SimDescriptors::ConfigurationDescriptor);
	ExpectBytes("composite configuration", FixtureCompositeUsb::Configuration, SimDescriptors::CompositeConfiguration);
	ExpectBytes("BOS descriptor", FixtureUsb::Bos, SimDescriptors::BosDescriptor);
	ExpectBytes("MS OS 2.0 set", FixtureUsb::MsOs20Set, SimDescriptors::MsOs20Set);

	// The strings of the fixture: language 0x0409, "Sim", "Bulk", "1", in the order the chain numbered them.
	const std::span<const std::span<const uint8>> strings = FixtureUsb::Set.strings;
	SIM_EXPECT_EQ(strings.size(), 4);
	for (std::size_t index = 0; index < strings.size() && index < 4; index++) {
		ExpectBytes("string descriptor", strings[index], SimDescriptors::StringTable[index]);
	}

	// The set the core answers from: the spans point at the static arrays, and the vendor request is the MS OS 2.0 one.
	SIM_EXPECT(FixtureUsb::Set.device.data() == FixtureUsb::Device.data());
	SIM_EXPECT_EQ(FixtureUsb::Set.vendorRequests.size(), 1);
	SIM_EXPECT_EQ(FixtureUsb::Set.vendorRequests[0].request, 0x42);
	SIM_EXPECT_EQ(FixtureUsb::Set.vendorRequests[0].index, 7);
	SIM_EXPECT(!FixtureUsb::Set.serialNumberAtRuntime);

	// Function layout of the fixture: interface 0, endpoints in the order the class requested them (IN then OUT), and the
	// slice of the configuration descriptor, which is the whole interface part.
	USBFunctionLayout layout = FixtureUsb::Function(0);
	SIM_EXPECT_EQ(layout.interfaces.size(), 1);
	SIM_EXPECT_EQ(layout.interfaces[0], 0);
	SIM_EXPECT_EQ(layout.endpoints.size(), 2);
	SIM_EXPECT_EQ(layout.endpoints[0], 0x81);
	SIM_EXPECT_EQ(layout.endpoints[1], 0x01);
	ExpectBytes("function descriptors", layout.descriptors, std::span<const uint8>(SimDescriptors::ConfigurationDescriptor).subspan(9));

	// Auto numbering: IN 1, then OUT takes 2 because 1 is used.
	USBFunctionLayout autoLayout = AutoUsb::Function(0);
	SIM_EXPECT_EQ(autoLayout.endpoints.size(), 2);
	SIM_EXPECT_EQ(autoLayout.endpoints[0], 0x81);
	SIM_EXPECT_EQ(autoLayout.endpoints[1], 0x02);

	// Interfaces are numbered in order across functions, and the second function starts at 1.
	USBFunctionLayout autoSecond = AutoUsb::Function(1);
	SIM_EXPECT_EQ(autoSecond.interfaces[0], 1);
	SIM_EXPECT_EQ(autoSecond.endpoints[0], 0x83);
	SIM_EXPECT_EQ(autoSecond.endpoints[1], 0x04);

	// A composite with an association: the device class is 0xEF/0x02/0x01, and the first function's slice starts with the
	// association descriptor (type 0x0B) over its two interfaces.
	SIM_EXPECT_EQ(AssociatedUsb::Device[4], 0xEF);
	SIM_EXPECT_EQ(AssociatedUsb::Device[5], 0x02);
	SIM_EXPECT_EQ(AssociatedUsb::Device[6], 0x01);
	USBFunctionLayout pair = AssociatedUsb::Function(0);
	SIM_EXPECT_EQ(pair.descriptors[1], 0x0B);
	SIM_EXPECT_EQ(pair.descriptors[2], 0);
	SIM_EXPECT_EQ(pair.descriptors[3], 2);

	// The whole configuration of the associated pair, byte for byte: the association's class, subclass, protocol and name, the
	// endpoint counts, and the shared "Pair" string on interfaces 0 and 2.
	ExpectBytes("associated configuration", AssociatedUsb::Configuration, AssociatedConfigurationArray);

	// Strings are numbered in order of first use and shared: "Pair" is string 2 (after the manufacturer "VHAL") and the second
	// function's name reuses it. Product and serial number follow.
	SIM_EXPECT_EQ(AssociatedUsb::Set.strings.size(), 5);
	SIM_EXPECT_EQ(AssociatedUsb::Set.strings[2][0], 2 + 2 * 4);
	SIM_EXPECT_EQ(AssociatedUsb::Set.configuration[0], 9);
	SIM_EXPECT_EQ(AssociatedUsb::Configuration[4], 3);

	// The interrupt function: one interrupt IN endpoint with its packet size and interval, and the self-powered and remote wakeup
	// bits of the configuration (0xE0).
	ExpectBytes("interrupt configuration", InterruptUsb::Configuration, InterruptConfigurationArray);
	SIM_EXPECT_EQ(InterruptUsb::Configuration[7], 0xE0);
	SIM_EXPECT_EQ(InterruptUsb::Device[4], 0x00);

	// Language 0x0407 answers string 0, "é" is one UTF-16 unit, and "😀" is a surrogate pair: D83D DE00.
	ExpectBytes("language 0x0407", StringsUsb::StringTable[0], GermanLanguage);
	ExpectBytes("accented manufacturer", StringsUsb::StringTable[1], AccentString);
	ExpectBytes("surrogate pair product", StringsUsb::StringTable[2], EmojiString);

	// Two WinUSB functions: one function subset each.
	ExpectBytes("two function subsets", TwoWinUsbUsb::MsOs20Set, TwoSubsetArray);
	SIM_EXPECT_EQ(TwoWinUsbUsb::Set.vendorRequests.size(), 1);

	// Two WinUSB functions with GUIDs: the set and its subset lengths, and the BOS that carries the set's length.
	std::array<uint8, 330> expectedTwoGuids{};
	std::copy(std::begin(TwoGuidSetHeader), std::end(TwoGuidSetHeader), expectedTwoGuids.begin());
	std::copy(std::begin(TwoGuidSubsetHeader0), std::end(TwoGuidSubsetHeader0), expectedTwoGuids.begin() + 10);
	std::copy(GuidCompatibleId.begin(), GuidCompatibleId.end(), expectedTwoGuids.begin() + 18);
	std::copy(GuidProperty.begin(), GuidProperty.end(), expectedTwoGuids.begin() + 38);
	std::copy(std::begin(TwoGuidSubsetHeader1), std::end(TwoGuidSubsetHeader1), expectedTwoGuids.begin() + 170);
	std::copy(GuidCompatibleId.begin(), GuidCompatibleId.end(), expectedTwoGuids.begin() + 178);
	std::copy(GuidPropertyTwo.begin(), GuidPropertyTwo.end(), expectedTwoGuids.begin() + 198);
	ExpectBytes("two function subsets with GUIDs", TwoWinUsbGuidUsb::MsOs20Set, expectedTwoGuids);
	SIM_EXPECT_EQ(TwoWinUsbGuidUsb::Bos[2], 0x21);
	SIM_EXPECT_EQ(TwoWinUsbGuidUsb::Bos[29], 0x4A);
	SIM_EXPECT_EQ(TwoWinUsbGuidUsb::Bos[30], 0x01);
	SIM_EXPECT_EQ(TwoWinUsbGuidUsb::Bos[31], 0x42);

	// A device-level set with a GUID property: the header, the compatible ID and the registry property.
	std::array<uint8, 162> expectedGuidSet{};
	std::copy(GuidSetHeader.begin(), GuidSetHeader.end(), expectedGuidSet.begin());
	std::copy(GuidCompatibleId.begin(), GuidCompatibleId.end(), expectedGuidSet.begin() + 10);
	std::copy(GuidProperty.begin(), GuidProperty.end(), expectedGuidSet.begin() + 30);
	ExpectBytes("device-level set with GUID", GuidUsb::MsOs20Set, expectedGuidSet);

	// A runtime serial number: the placeholder string (an empty descriptor) answers index 3 and the flag is set.
	CheckRuntimeSerial();

	// The vendor functions take the layout: the numbers and addresses of the function are the layout's own.
	VendorBulkFunction<64, 64, 1> bulk(FixtureUsb::Function(0));
	SIM_EXPECT_EQ(bulk.GetInterfaces().size(), 1);
	SIM_EXPECT_EQ(bulk.GetInterfaces()[0], 0);
	SIM_EXPECT(bulk.OwnsEndpoint(0x81));
	SIM_EXPECT(bulk.OwnsEndpoint(0x01));
	VendorBulkStreamFunction<1> stream(AutoUsb::Function(1));
	SIM_EXPECT(stream.OwnsEndpoint(0x83));
	SIM_EXPECT(stream.OwnsEndpoint(0x04));

	// The extension alone builds a BOS of its capability, with no MS OS 2.0 set and no vendor request.
	ExpectBytes("USB 2.0 extension BOS", Usb2OnlyUsb::Bos, Usb2OnlyBos);
	SIM_EXPECT(Usb2OnlyUsb::MsOs20Set.empty());
	SIM_EXPECT(Usb2OnlyUsb::Set.vendorRequests.empty());
	SIM_EXPECT_EQ(Usb2OnlyUsb::Device[2], 0x10);
	SIM_EXPECT_EQ(Usb2OnlyUsb::Device[3], 0x02);

	// The composite without BOS: no vendor request, and the device class is per interface.
	SIM_EXPECT(FixtureCompositeUsb::Set.bos.empty());
	SIM_EXPECT(FixtureCompositeUsb::Set.vendorRequests.empty());
	SIM_EXPECT_EQ(FixtureCompositeUsb::Device[4], 0x00);

	return SimTest::Finish("TestFluentBuilder");
}
