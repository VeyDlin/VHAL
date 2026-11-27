#include <VHAL.h>
#include "DeviceFixture.h"
#include "Descriptors.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <Drivers/Interface/USB/Descriptor/USBDescriptorSet.h>
#include <Drivers/Interface/USB/Descriptor/USBSerialNumber.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <array>
#include <cstddef>
#include <span>


// The run-time serial number: SetSerialNumber() answers iSerialNumber from the application's bytes, and the descriptor set
// reserves that index. The cases are the served bytes and their truncation, the rejected descriptors, the refusal of Start() without
// a serial or with index 0, the refusal of SetSerialNumber() on a set that does not reserve one, the busy refusal after Start(), the
// unchanged behaviour when no run-time serial is given, and the bytes of MakeUSBSerialNumberDescriptor() for a known ID.


namespace {


// The unique ID 12 AB CD, as the MCU would report it, and its serial number string: bLength 14 (2 + 4 * 3), type 3, then each
// byte as two upper-case hex digits in UTF-16LE.
constexpr std::array<uint8, 3> UniqueId = { 0x12, 0xAB, 0xCD };


// Runs the helper at compile time, so its constexpr use is checked by the compiler as well as by the run-time cases below.
constexpr ResultStatus FillSerialForId(std::array<uint8, 14>& bytes) {
	return MakeUSBSerialNumberDescriptor(UniqueId, bytes);
}


constexpr ResultStatus SerialForIdStatus() {
	std::array<uint8, 14> bytes{};
	return FillSerialForId(bytes);
}


static_assert(SerialForIdStatus() == ResultStatus::ok, "the helper runs in a constant expression");


constexpr std::array<uint8, 14> MakeSerialForId() {
	std::array<uint8, 14> bytes{};
	FillSerialForId(bytes);
	return bytes;
}


inline constexpr std::array<uint8, 14> SerialForId = MakeSerialForId();


// Hand-written: '1' '2' 'A' 'B' 'C' 'D' as UTF-16LE, after bLength and bDescriptorType.
inline constexpr uint8 ExpectedSerialForId[] = {
	0x0E, 0x03,
	0x31, 0x00, 0x32, 0x00,
	0x41, 0x00, 0x42, 0x00,
	0x43, 0x00, 0x44, 0x00
};


static_assert(sizeof(ExpectedSerialForId) == 14, "the expected serial number is 14 bytes");


// The device descriptor of the shared set with iSerialNumber changed. Index 0 means no serial number.
constexpr std::array<uint8, 18> MakeDeviceWithSerialIndex(uint8 serialIndex) {
	std::array<uint8, 18> bytes{};
	for (std::size_t index = 0; index < bytes.size(); index++) {
		bytes[index] = SimDescriptors::DeviceDescriptor[index];
	}

	bytes[16] = serialIndex;
	return bytes;
}


inline constexpr std::array<uint8, 18> DeviceWithoutSerialIndex = MakeDeviceWithSerialIndex(0);


// The shared set with iSerialNumber reserved for the run-time string. Its strings table keeps the placeholder at index 3.
inline constexpr USBDescriptorSet RuntimeSet = {
	SimDescriptors::DeviceDescriptor,
	SimDescriptors::ConfigurationDescriptor,
	SimDescriptors::StringTable,
	SimDescriptors::BosDescriptor,
	SimDescriptors::VendorRequests,
	true
};


// Reserves iSerialNumber but the device descriptor names no index for it.
inline constexpr USBDescriptorSet RuntimeSetWithoutIndex = {
	DeviceWithoutSerialIndex,
	SimDescriptors::ConfigurationDescriptor,
	SimDescriptors::StringTable,
	SimDescriptors::BosDescriptor,
	SimDescriptors::VendorRequests,
	true
};


// Not a string descriptor: bLength 2 but size 1.
inline constexpr uint8 TooShort[] = { 0x02 };
// bLength 4 for six bytes.
inline constexpr uint8 LengthMismatch[] = { 0x04, 0x03, 0x31, 0x00, 0x32, 0x00 };
// bDescriptorType 4, not a string.
inline constexpr uint8 WrongType[] = { 0x04, 0x04, 0x31, 0x00 };
// bLength equals the odd size, which no UTF-16 text has.
inline constexpr uint8 OddSize[] = { 0x05, 0x03, 0x31, 0x00, 0x32 };
// A string descriptor with no characters: the serial would be empty, and every board with it would report the same serial.
inline constexpr uint8 EmptyText[] = { 0x02, 0x03 };


// A device with the set and the test function, as an application builds it.
struct SerialFixture {
	SimUSBDeviceAdapter sim;
	TestFunction function;
	std::array<USBFunction*, 1> functions;
	USBDevice device;


	explicit SerialFixture(const USBDescriptorSet& set)
		: function(SimFixtureData::Interfaces, SimFixtureData::Endpoints, 0x81, 0x01),
		  functions{ &function },
		  device(sim, set, functions) {
	}
};


// Reads the serial number string (index 3, language 0x0409) with the given wLength and checks the status and the bytes.
void ExpectSerialString(SimUSBDeviceAdapter& sim, uint16 length, std::span<const uint8> expected) {
	std::array<uint8, 255> in = {};
	uint32 received = 0;
	USBSetupBytes setup = SimHost::MakeSetup(0x80, 6, 0x0303, 0x0409, length);
	SIM_EXPECT_EQ(SimHost::Control(sim, setup, std::span<const uint8>(), in, received), ResultStatus::ok);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(received), expected));
}


// Reads a string descriptor by index and language and checks the status and the bytes.
void ExpectString(SimUSBDeviceAdapter& sim, uint16 index, uint16 language, std::span<const uint8> expected) {
	std::array<uint8, 255> in = {};
	uint32 received = 0;
	uint16 value = static_cast<uint16>(0x0300 | index);
	USBSetupBytes setup = SimHost::MakeSetup(0x80, 6, value, language, 255);
	SIM_EXPECT_EQ(SimHost::Control(sim, setup, std::span<const uint8>(), in, received), ResultStatus::ok);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(received), expected));
}


void TestRuntimeSerialIsServed() {
	SerialFixture fixture(RuntimeSet);
	SIM_EXPECT_EQ(fixture.device.SetSerialNumber(SerialForId), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.device.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);
	fixture.sim.InjectBusReset();

	// The whole descriptor, then a read that asks for less: the host gets the first wLength bytes.
	ExpectSerialString(fixture.sim, 255, SerialForId);
	ExpectSerialString(fixture.sim, 6, std::span<const uint8>(SerialForId).first(6));
	ExpectSerialString(fixture.sim, 2, std::span<const uint8>(SerialForId).first(2));

	// Any language ID gets the same string: the core ignores it for strings, as it does for the static ones.
	ExpectString(fixture.sim, 3, 0x0407, SerialForId);

	// The other strings are unchanged, and index 0 is still the language list.
	ExpectString(fixture.sim, 1, 0x0409, SimDescriptors::StringManufacturer);
	ExpectString(fixture.sim, 0, 0x0000, SimDescriptors::StringLanguage);
}


void TestRejectedSerialNumbers() {
	SerialFixture fixture(RuntimeSet);
	SIM_EXPECT_EQ(fixture.device.SetSerialNumber(std::span<const uint8>(TooShort)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.device.SetSerialNumber(std::span<const uint8>()), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.device.SetSerialNumber(std::span<const uint8>(LengthMismatch)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.device.SetSerialNumber(std::span<const uint8>(WrongType)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.device.SetSerialNumber(std::span<const uint8>(OddSize)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.device.SetSerialNumber(std::span<const uint8>(EmptyText)), ResultStatus::invalidParameter);

	// No rejected call kept its span: the device still has no serial number to start with.
	SIM_EXPECT_EQ(fixture.device.Start(USBDeviceAdapter::Parameters()), ResultStatus::invalidParameter);
}


void TestStartRefusedWithoutSerialNumber() {
	SerialFixture fixture(RuntimeSet);
	SIM_EXPECT_EQ(fixture.device.Start(USBDeviceAdapter::Parameters()), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Detached);

	// Once the serial number is given, the same device starts.
	SIM_EXPECT_EQ(fixture.device.SetSerialNumber(SerialForId), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.device.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);
}


void TestStartRefusedWithoutIndex() {
	SerialFixture fixture(RuntimeSetWithoutIndex);
	SIM_EXPECT_EQ(fixture.device.SetSerialNumber(SerialForId), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.device.Start(USBDeviceAdapter::Parameters()), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Detached);
}


void TestSetSerialNumberBusyAfterStart() {
	SerialFixture fixture(RuntimeSet);
	SIM_EXPECT_EQ(fixture.device.SetSerialNumber(SerialForId), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.device.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);

	// A started device keeps the serial number it was given, even for a valid replacement.
	SIM_EXPECT_EQ(fixture.device.SetSerialNumber(SerialForId), ResultStatus::busy);
	SIM_EXPECT_EQ(fixture.device.SetSerialNumber(std::span<const uint8>(TooShort)), ResultStatus::busy);
	fixture.sim.InjectBusReset();
	ExpectSerialString(fixture.sim, 255, SerialForId);
}


// Without a run-time serial number the set answers as it always did: the static string at the index is served.
void TestUnchangedWithoutRuntimeSerial() {
	SerialFixture fixture(SimDescriptors::SimpleSet);
	SIM_EXPECT_EQ(fixture.device.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);
	fixture.sim.InjectBusReset();
	ExpectSerialString(fixture.sim, 255, SimDescriptors::StringSerial);
}


// Without the flag, SetSerialNumber() is refused, so a static serial is never replaced. The static serial still answers, and a set
// with no index for iSerialNumber keeps its language list at index 0.
void TestRuntimeSerialRefusedWithoutFlag() {
	SerialFixture withStatic(SimDescriptors::SimpleSet);
	SIM_EXPECT_EQ(withStatic.device.SetSerialNumber(SerialForId), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(withStatic.device.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);
	withStatic.sim.InjectBusReset();
	ExpectSerialString(withStatic.sim, 255, SimDescriptors::StringSerial);

	SerialFixture withoutIndex(USBDescriptorSet{
		DeviceWithoutSerialIndex,
		SimDescriptors::ConfigurationDescriptor,
		SimDescriptors::StringTable,
		SimDescriptors::BosDescriptor,
		SimDescriptors::VendorRequests
	});
	SIM_EXPECT_EQ(withoutIndex.device.SetSerialNumber(SerialForId), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(withoutIndex.device.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);
	withoutIndex.sim.InjectBusReset();
	ExpectString(withoutIndex.sim, 0, 0x0000, SimDescriptors::StringLanguage);
}


void TestHelperBytesForKnownId() {
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(SerialForId), std::span<const uint8>(ExpectedSerialForId)));

	// A buffer one byte short is refused, and nothing is written past the size the call needs.
	std::array<uint8, 13> small = {};
	SIM_EXPECT_EQ(MakeUSBSerialNumberDescriptor(UniqueId, small), ResultStatus::invalidParameter);

	// An empty unique ID would give every board the same empty serial, so the helper refuses it.
	std::array<uint8, 2> empty = {};
	SIM_EXPECT_EQ(MakeUSBSerialNumberDescriptor(std::span<const uint8>(), empty), ResultStatus::invalidParameter);
}


// At run time, with 63 bytes: 2 + 4 * 63 = 254 bytes, the largest ID that fits bLength. 64 bytes would need 258.
void TestHelperAtRunTimeAndSizeLimit() {
	std::array<uint8, 63> id = {};
	for (std::size_t index = 0; index < id.size(); index++) {
		id[index] = static_cast<uint8>(index);
	}

	std::array<uint8, 300> bytes = {};
	SIM_EXPECT_EQ(MakeUSBSerialNumberDescriptor(id, bytes), ResultStatus::ok);
	SIM_EXPECT_EQ(bytes[0], 254U);
	SIM_EXPECT_EQ(bytes[1], 0x03U);
	// Byte 0x00 is "00", and byte 0x3E (the last one) is "3E".
	SIM_EXPECT_EQ(bytes[2], 0x30U);
	SIM_EXPECT_EQ(bytes[6], 0x30U);
	SIM_EXPECT_EQ(bytes[7], 0x00U);
	SIM_EXPECT_EQ(bytes[250], 0x33U);
	SIM_EXPECT_EQ(bytes[252], 0x45U);

	std::array<uint8, 300> bytesTooLong = {};
	std::array<uint8, 64> tooMany = {};
	SIM_EXPECT_EQ(MakeUSBSerialNumberDescriptor(tooMany, bytesTooLong), ResultStatus::invalidParameter);
}


}


int main() {
	TestRuntimeSerialIsServed();
	TestRejectedSerialNumbers();
	TestStartRefusedWithoutSerialNumber();
	TestStartRefusedWithoutIndex();
	TestSetSerialNumberBusyAfterStart();
	TestUnchangedWithoutRuntimeSerial();
	TestRuntimeSerialRefusedWithoutFlag();
	TestHelperBytesForKnownId();
	TestHelperAtRunTimeAndSizeLimit();
	return SimTest::Finish("TestSerialNumber");
}
