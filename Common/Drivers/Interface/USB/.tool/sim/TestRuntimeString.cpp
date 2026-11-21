#include <VHAL.h>
#include "Descriptors.h"
#include "RuntimeStringFunction.h"
#include "SimAssert.h"
#include "SimHost.h"
#include "TestFunction.h"
#include <Drivers/Interface/USB/Descriptor/Builder/USBConfigurationBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorSet.h>
#include <Drivers/Interface/USB/Descriptor/USBHexStringDescriptor.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <array>
#include <cstddef>
#include <span>


// Run-time strings: a class builder reserves two, the chain numbers them after the three fixed strings, and the descriptor of each
// carries its index. The cases are the placement of the indexes in the configuration and the layout, the refusal of rejected
// descriptors and of indexes the set does not reserve, the refusal of Start() until every reserved string has a descriptor, the
// replacement before Start(), the served bytes and their truncation, the busy refusal after Start(), and the helper's bytes for a
// 6-byte MAC address.


namespace {


// Two run-time strings from one class builder, on the device of the fixed strings "Sim", "Bulk" and "1". So the strings are numbered
// 4 and 5, after the fixed ones 1 to 3, and the placeholders at 4 and 5 are empty descriptors until SetRuntimeString() gives them.
using RuntimeUsb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Bulk")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(RuntimeStringFunction<2>()));
}>;


// Two functions, one run-time string each, on two interfaces: the strings are numbered 4 and 5, and each function gets its own.
using MultiUsb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Bulk")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(RuntimeStringFunction<1, 1>())
			.Function(RuntimeStringFunction<1, 2>()));
}>;


static_assert(RuntimeUsb::Set.runtimeStringIndexes.size() == 2, "two run-time strings are reserved");
static_assert(RuntimeUsb::RuntimeStringIndexes[0] == 4, "the first run-time string follows the three fixed strings");
static_assert(RuntimeUsb::RuntimeStringIndexes[1] == 5, "the second run-time string follows the first");
static_assert(RuntimeUsb::Set.strings.size() == 6, "the language list, three fixed strings and two placeholders");
static_assert(RuntimeUsb::Set.strings[4].size() == 2, "a run-time placeholder is an empty string descriptor");


constexpr USBFunctionLayout RuntimeLayout = RuntimeUsb::Function(0);
static_assert(RuntimeLayout.runtimeStrings.size() == 2, "the function knows both of its run-time strings");
static_assert(RuntimeLayout.runtimeStrings[0] == 4, "the function's first run-time string is 4");
static_assert(RuntimeLayout.runtimeStrings[1] == 5, "the function's second run-time string is 5");


// Hand-written configuration of RuntimeUsb: one vendor interface with the bulk pair 0x81 and 0x01, and the run-time strings 4 and 5
// in the class's functional descriptors, at offset 3 with their position in the function at offset 4. wTotalLength 42.
inline constexpr uint8 ExpectedConfiguration[] = {
	0x09, 0x02, 0x2A, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,	// configuration: 42 bytes, 1 interface, value 1, bus powered, 100 mA
	0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00,	// interface 0: vendor class, 2 endpoints
	0x05, 0x24, 0x7F, 0x04, 0x00,							// functional descriptor, run-time string 4, position 0
	0x05, 0x24, 0x7F, 0x05, 0x01,							// functional descriptor, run-time string 5, position 1
	0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,				// endpoint 0x81: bulk IN, 64 bytes
	0x07, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00				// endpoint 0x01: bulk OUT, 64 bytes
};


static_assert(sizeof(ExpectedConfiguration) == 42, "the configuration is 42 bytes");


// Hand-written configuration of MultiUsb: two interfaces. The first function's string is 4 and the second's is 5, and each function
// numbers its own position from 0. wTotalLength 65.
inline constexpr uint8 ExpectedMultiConfiguration[] = {
	0x09, 0x02, 0x41, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,	// configuration: 65 bytes, 2 interfaces, value 1, bus powered, 100 mA
	0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00,	// interface 0: vendor class, 2 endpoints
	0x05, 0x24, 0x7F, 0x04, 0x00,							// functional descriptor of function 0: run-time string 4, position 0
	0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,				// endpoint 0x81: bulk IN
	0x07, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00,				// endpoint 0x01: bulk OUT
	0x09, 0x04, 0x01, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00,	// interface 1: vendor class, 2 endpoints
	0x05, 0x24, 0x7F, 0x05, 0x00,							// functional descriptor of function 1: run-time string 5, position 0
	0x07, 0x05, 0x82, 0x02, 0x40, 0x00, 0x00,				// endpoint 0x82: bulk IN
	0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00				// endpoint 0x02: bulk OUT
};


static_assert(sizeof(ExpectedMultiConfiguration) == 65, "the composite configuration is 65 bytes");


static_assert(MultiUsb::Function(0).runtimeStrings[0] == 4, "function 0 reserves string 4");
static_assert(MultiUsb::Function(1).runtimeStrings[0] == 5, "function 1 reserves string 5");
static_assert(MultiUsb::Function(1).runtimeStrings.size() == 1, "function 1 reserves one string");


// A 6-byte MAC address, as iMACAddress names it: 02 00 00 AB CD EF is the text "020000ABCDEF".
inline constexpr std::array<uint8, 6> MacAddress = { 0x02, 0x00, 0x00, 0xAB, 0xCD, 0xEF };


// Runs the helper at compile time, so its constexpr use is checked by the compiler as well as by the run-time cases below.
constexpr std::array<uint8, 26> MakeMacDescriptor() {
	std::array<uint8, 26> bytes{};
	MakeUSBHexStringDescriptor(MacAddress, bytes);
	return bytes;
}


inline constexpr std::array<uint8, 26> MacDescriptor = MakeMacDescriptor();


// Hand-written: bLength 26 (2 + 4 * 6), type 3, then "020000ABCDEF" as UTF-16LE: each character is its ASCII code and a zero byte.
inline constexpr uint8 ExpectedMacDescriptor[] = {
	0x1A, 0x03,
	0x30, 0x00, 0x32, 0x00, 0x30, 0x00, 0x30, 0x00, 0x30, 0x00, 0x30, 0x00,
	0x41, 0x00, 0x42, 0x00, 0x43, 0x00, 0x44, 0x00, 0x45, 0x00, 0x46, 0x00
};


static_assert(sizeof(ExpectedMacDescriptor) == 26, "the MAC descriptor is 26 bytes");


// The text given to the second run-time string, as a string descriptor: "Run" as UTF-16LE.
inline constexpr uint8 TextDescriptor[] = {
	0x08, 0x03, 0x52, 0x00, 0x75, 0x00, 0x6E, 0x00
};


// Not string descriptors, for the refusals of SetRuntimeString().
// bLength 2 but size 1.
inline constexpr uint8 TooShort[] = { 0x02 };
// bLength 4 for six bytes.
inline constexpr uint8 LengthMismatch[] = { 0x04, 0x03, 0x31, 0x00, 0x32, 0x00 };
// bDescriptorType 4, not a string.
inline constexpr uint8 WrongType[] = { 0x04, 0x04, 0x31, 0x00 };
// bLength equals the odd size, which no UTF-16 text has.
inline constexpr uint8 OddSize[] = { 0x05, 0x03, 0x31, 0x00, 0x32 };


// A device with the chain's set and the test function on its interface, as an application builds it.
struct RuntimeFixture {
	SimUSBDeviceAdapter sim;
	TestFunction function;
	std::array<USBFunction*, 1> functions;
	USBDevice device;


	RuntimeFixture()
		: function(RuntimeLayout.interfaces, RuntimeLayout.endpoints, 0x81, 0x01),
		  functions{ &function },
		  device(sim, RuntimeUsb::Set, functions) {
	}
};


// Reads a string descriptor by index and language and checks the status and the bytes.
void ExpectString(SimUSBDeviceAdapter& sim, uint16 index, uint16 language, uint16 length, std::span<const uint8> expected) {
	std::array<uint8, 255> in = {};
	uint32 received = 0;
	uint16 value = static_cast<uint16>(0x0300 | index);
	USBSetupBytes setup = SimHost::MakeSetup(0x80, 6, value, language, length);
	SIM_EXPECT_EQ(SimHost::Control(sim, setup, std::span<const uint8>(), in, received), ResultStatus::ok);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(received), expected));
}


// The configuration as GET_DESCRIPTOR(CONFIGURATION) answers it, checked against the bytes of the test's own.
void ExpectConfigurationBytes(SimUSBDeviceAdapter& sim, std::span<const uint8> expected) {
	std::array<uint8, 255> in = {};
	uint32 received = 0;
	USBSetupBytes setup = SimHost::MakeSetup(0x80, 6, 0x0200, 0x0000, 255);
	SIM_EXPECT_EQ(SimHost::Control(sim, setup, std::span<const uint8>(), in, received), ResultStatus::ok);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(received), expected));
}


void TestIndexesInConfiguration() {
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(RuntimeUsb::Configuration), std::span<const uint8>(ExpectedConfiguration)));
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(MultiUsb::Configuration), std::span<const uint8>(ExpectedMultiConfiguration)));
	SIM_EXPECT_EQ(MultiUsb::Function(0).runtimeStrings.size(), 1);
	SIM_EXPECT_EQ(MultiUsb::Function(0).runtimeStrings[0], 4);
	SIM_EXPECT_EQ(MultiUsb::Function(1).runtimeStrings[0], 5);
	SIM_EXPECT_EQ(MultiUsb::Set.runtimeStringIndexes.size(), 2);
	SIM_EXPECT_EQ(MultiUsb::Set.runtimeStringIndexes[1], 5);
}


// Only the two placeholders are added to the chain's strings: the fixed strings keep their text and their numbers.
void TestStringTableAfterFixedStrings() {
	SIM_EXPECT_EQ(RuntimeUsb::Set.strings.size(), 6);
	SIM_EXPECT(SimHost::SameBytes(RuntimeUsb::Set.strings[1], SimDescriptors::StringManufacturer));
	SIM_EXPECT(SimHost::SameBytes(RuntimeUsb::Set.strings[2], SimDescriptors::StringProduct));
	SIM_EXPECT(SimHost::SameBytes(RuntimeUsb::Set.strings[3], SimDescriptors::StringSerial));
	SIM_EXPECT(!RuntimeUsb::Set.serialNumberAtRuntime);
}


// SetRuntimeString() refuses an index the set does not reserve (the fixed serial's 3, the language's 0, and past the end), and every
// descriptor that is not a string descriptor. A refused call keeps nothing: Start() is still refused afterwards.
void TestRefusedRuntimeStrings() {
	RuntimeFixture fixture;
	SIM_EXPECT_EQ(fixture.device.SetRuntimeString(3, SimDescriptors::StringSerial), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.device.SetRuntimeString(0, TextDescriptor), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.device.SetRuntimeString(6, TextDescriptor), ResultStatus::invalidParameter);

	SIM_EXPECT_EQ(fixture.device.SetRuntimeString(4, std::span<const uint8>(TooShort)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.device.SetRuntimeString(4, std::span<const uint8>(LengthMismatch)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.device.SetRuntimeString(4, std::span<const uint8>(WrongType)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.device.SetRuntimeString(4, std::span<const uint8>(OddSize)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.device.SetRuntimeString(4, std::span<const uint8>()), ResultStatus::invalidParameter);

	SIM_EXPECT_EQ(fixture.device.Start(USBDeviceAdapter::Parameters()), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Detached);
}


// Start() is refused while either reserved string has no descriptor, and accepted once both have one. The refused Start() leaves the
// device detached, so the same device starts after the last string is given.
void TestStartRefusedUntilEveryStringGiven() {
	RuntimeFixture fixture;
	SIM_EXPECT_EQ(fixture.device.Start(USBDeviceAdapter::Parameters()), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.device.SetRuntimeString(4, MacDescriptor), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.device.Start(USBDeviceAdapter::Parameters()), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Detached);

	SIM_EXPECT_EQ(fixture.device.SetRuntimeString(5, TextDescriptor), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.device.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);

	// Once started, the strings cannot change.
	SIM_EXPECT_EQ(fixture.device.SetRuntimeString(4, TextDescriptor), ResultStatus::busy);
	SIM_EXPECT_EQ(fixture.device.SetRuntimeString(5, MacDescriptor), ResultStatus::busy);
}


// The strings the host reads are the ones the application gave: the whole string, a truncated read, the language being ignored as for the
// fixed strings, and the fixed strings are unchanged. A later call before Start() replaces an earlier one.
void TestServesRuntimeStrings() {
	RuntimeFixture fixture;
	SIM_EXPECT_EQ(fixture.device.SetRuntimeString(4, TextDescriptor), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.device.SetRuntimeString(4, MacDescriptor), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.device.SetRuntimeString(5, TextDescriptor), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.device.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);
	fixture.sim.InjectBusReset();

	ExpectString(fixture.sim, 4, 0x0409, 255, MacDescriptor);
	ExpectString(fixture.sim, 4, 0x0407, 6, std::span<const uint8>(MacDescriptor).first(6));
	ExpectString(fixture.sim, 5, 0x0409, 255, TextDescriptor);
	ExpectString(fixture.sim, 1, 0x0409, 255, SimDescriptors::StringManufacturer);
	ExpectString(fixture.sim, 3, 0x0409, 255, SimDescriptors::StringSerial);
	ExpectString(fixture.sim, 0, 0x0000, 255, SimDescriptors::StringLanguage);
	ExpectConfigurationBytes(fixture.sim, std::span<const uint8>(ExpectedConfiguration));
}


// A two-byte descriptor is an empty string, which the serial rule refuses, so the run-time string refuses it too. A string of one
// character, 4 bytes, is the shortest accepted: bLength 4, type 3, the character 1 as UTF-16LE.
void TestEmptyDescriptorRefused() {
	RuntimeFixture fixture;
	static constexpr uint8 EmptyDescriptor[] = { 0x02, 0x03 };
	static constexpr uint8 OneCharacter[] = { 0x04, 0x03, 0x31, 0x00 };
	SIM_EXPECT_EQ(fixture.device.SetRuntimeString(4, std::span<const uint8>(EmptyDescriptor)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.device.SetRuntimeString(4, std::span<const uint8>(OneCharacter)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.device.SetRuntimeString(5, MacDescriptor), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.device.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);
	fixture.sim.InjectBusReset();
	ExpectString(fixture.sim, 4, 0x0409, 255, std::span<const uint8>(OneCharacter));
}


// The helper gives the hand-written bytes of the MAC address, and refuses a buffer one byte short and an empty value.
void TestMacHelper() {
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(MacDescriptor), std::span<const uint8>(ExpectedMacDescriptor)));

	std::array<uint8, 25> small = {};
	SIM_EXPECT_EQ(MakeUSBHexStringDescriptor(MacAddress, small), ResultStatus::invalidParameter);

	std::array<uint8, 26> bytes = {};
	SIM_EXPECT_EQ(MakeUSBHexStringDescriptor(std::span<const uint8>(), bytes), ResultStatus::invalidParameter);
}


// Each input byte is 4 bytes of UTF-16, plus the 2-byte header, and bLength is one byte: 63 input bytes give 2 + 252 = 254 bytes, the
// largest accepted, and 64 give 2 + 256 = 258, which bLength cannot describe, so it is refused.
void TestHexLengthLimit() {
	std::array<uint8, 63> largest = {};
	largest.fill(0xAB);
	std::array<uint8, 254> largestOut = {};
	SIM_EXPECT_EQ(MakeUSBHexStringDescriptor(largest, largestOut), ResultStatus::ok);
	SIM_EXPECT_EQ(static_cast<std::size_t>(largestOut[0]), 254);
	SIM_EXPECT_EQ(largestOut[1], 0x03);
	SIM_EXPECT_EQ(largestOut[253], 0x00);
	SIM_EXPECT_EQ(largestOut[252], 0x42);

	std::array<uint8, 64> tooLong = {};
	tooLong.fill(0xAB);
	std::array<uint8, 258> tooLongOut = {};
	SIM_EXPECT_EQ(MakeUSBHexStringDescriptor(tooLong, tooLongOut), ResultStatus::invalidParameter);
}


}


int main() {
	TestIndexesInConfiguration();
	TestStringTableAfterFixedStrings();
	TestRefusedRuntimeStrings();
	TestStartRefusedUntilEveryStringGiven();
	TestServesRuntimeStrings();
	TestEmptyDescriptorRefused();
	TestMacHelper();
	TestHexLengthLimit();
	return SimTest::Finish("TestRuntimeString");
}
