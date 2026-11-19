#include <VHAL.h>
#include "MscHost.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <array>
#include <cstddef>
#include <span>


// A mass storage device enumerated the way the three host families run it, and then used to mount its volume: GET_MAX_LUN, a
// TEST UNIT READY loop, INQUIRY, READ CAPACITY(10), READ FORMAT CAPACITIES, MODE SENSE(6), and a READ(10) of block 0. The volume is
// the FAT12 image of MscTest::SmallVolume, so block 0 must come back as its boot sector. The bytes of the descriptors are the
// hand-written ones of TestMscDescriptors.


namespace {


using Device = MscTest::RamMscDevice<512, 128>;
using MscTest::BotHost;
using MscTest::Outcome;


// The device descriptor, 18 bytes, and the first answers of the configuration.
constexpr std::array<uint8, 18> DeviceBytes{
	0x12, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x40, 0x09, 0x12, 0x01, 0x00, 0x00, 0x01, 0x01, 0x02, 0x03, 0x01
};


constexpr std::array<uint8, 9> ConfigurationHeader{ 0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32 };


constexpr std::array<uint8, 32> Configuration{
	0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
	0x09, 0x04, 0x00, 0x00, 0x02, 0x08, 0x06, 0x50, 0x02,
	0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,
	0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00
};


// The language list and the start of the product string ("Mass storage": 26 bytes, UTF-16LE).
constexpr std::array<uint8, 4> Language{ 0x04, 0x03, 0x09, 0x04 };
constexpr std::array<uint8, 4> ProductStart{ 0x1A, 0x03, 0x4D, 0x00 };


// The INQUIRY answer of the test identity (SPC-4 table 151).
constexpr std::array<uint8, 36> InquiryAnswer{
	0x00, 0x80, 0x04, 0x02, 0x1F, 0x00, 0x00, 0x00,
	'V', 'H', 'A', 'L', ' ', ' ', ' ', ' ',
	'R', 'A', 'M', ' ', 'D', 'i', 's', 'k', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
	'1', '.', '0', '0'
};


// The answers the host reads with a control request: the bytes the device sent, checked against the expected ones.
void ExpectIn(SimUSBDeviceAdapter& sim, const USBSetupBytes& setup, std::span<const uint8> expected) {
	std::array<uint8, 256> in{};
	uint32 received = 0;
	SIM_EXPECT_EQ(SimHost::Control(sim, setup, std::span<const uint8>(), in, received), ResultStatus::ok);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(received), expected));
}


// Reads the device descriptor and sets the address, the way the host does after its first bus reset.
void Address(SimUSBDeviceAdapter& sim, uint8 address) {
	sim.InjectBusReset();
	ExpectIn(sim, SimHost::MakeSetup(0x80, 6, 0x0100, 0x0000, 64), DeviceBytes);
	SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0x00, 5, address, 0x0000, 0)), ResultStatus::ok);
}


void Configure(SimUSBDeviceAdapter& sim) {
	SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0)), ResultStatus::ok);
}


// The strings and the configuration, read in the steps of the host. The configuration is read in two parts: the header first, then
// the whole wTotalLength.
void Describe(SimUSBDeviceAdapter& sim) {
	ExpectIn(sim, SimHost::MakeSetup(0x80, 6, 0x0200, 0x0000, 9), ConfigurationHeader);
	ExpectIn(sim, SimHost::MakeSetup(0x80, 6, 0x0200, 0x0000, 255), Configuration);
	ExpectIn(sim, SimHost::MakeSetup(0x80, 6, 0x0300, 0x0000, 255), Language);
	ExpectIn(sim, SimHost::MakeSetup(0x80, 6, 0x0302, 0x0409, 4), ProductStart);
}


// One TEST UNIT READY, sent with a fresh tag. The status is the CSW's.
ResultStatus TestUnitReady(BotHost& host, uint32 tag) {
	const std::array<uint8, 6> cdb{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	Outcome outcome = host.Command(tag, cdb, 0, false, {}, {});
	if (outcome.cswStatus != ResultStatus::ok) {
		return outcome.cswStatus;
	}

	return outcome.csw.status == MscStatusPassed ? ResultStatus::ok : ResultStatus::error;
}


void ExpectCommandPasses(BotHost& host, uint32 tag, std::span<const uint8> cdb, uint32 length, bool dataIn, std::span<uint8> in) {
	Outcome outcome = host.Command(tag, cdb, length, dataIn, {}, in);
	SIM_EXPECT_EQ(outcome.csw.status, MscStatusPassed);
	SIM_EXPECT_EQ(outcome.csw.residue, 0);
}


// The mounting reads after the configuration: INQUIRY, READ CAPACITY, READ FORMAT CAPACITIES, MODE SENSE(6) and a READ(10) of block 0.
void MountVolume(BotHost& host, uint32 firstTag) {
	std::array<uint8, 36> inquiry{};
	const std::array<uint8, 6> inquiryCdb{ 0x12, 0x00, 0x00, 0x00, 36, 0x00 };
	ExpectCommandPasses(host, firstTag, inquiryCdb, 36, true, inquiry);
	SIM_EXPECT(SimHost::SameBytes(inquiry, InquiryAnswer));

	std::array<uint8, 8> capacity{};
	const std::array<uint8, 10> capacityCdb{ 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	ExpectCommandPasses(host, firstTag + 1, capacityCdb, 8, true, capacity);
	const std::array<uint8, 8> expectedCapacity{ 0x00, 0x00, 0x00, 0x7F, 0x00, 0x00, 0x02, 0x00 };
	SIM_EXPECT(SimHost::SameBytes(capacity, expectedCapacity));

	std::array<uint8, 12> formats{};
	const std::array<uint8, 10> formatsCdb{ 0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 12, 0x00 };
	ExpectCommandPasses(host, firstTag + 2, formatsCdb, 12, true, formats);
	const std::array<uint8, 12> expectedFormats{ 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x80, 0x02, 0x00, 0x02, 0x00 };
	SIM_EXPECT(SimHost::SameBytes(formats, expectedFormats));

	std::array<uint8, 4> mode{};
	const std::array<uint8, 6> modeCdb{ 0x1A, 0x00, 0x3F, 0x00, 4, 0x00 };
	ExpectCommandPasses(host, firstTag + 3, modeCdb, 4, true, mode);
	const std::array<uint8, 4> expectedMode{ 0x03, 0x00, 0x00, 0x00 };
	SIM_EXPECT(SimHost::SameBytes(mode, expectedMode));

	// READ(10) of block 0: 512 bytes, the boot sector of the volume.
	std::array<uint8, 512> boot{};
	const std::array<uint8, 10> readCdb{ 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00 };
	ExpectCommandPasses(host, firstTag + 4, readCdb, 512, true, boot);
	SIM_EXPECT(SimHost::SameBytes(boot, std::span<const uint8>(MscTest::SmallVolume::Bytes).first(512)));
	SIM_EXPECT_EQ(boot[510], 0x55);
	SIM_EXPECT_EQ(boot[511], 0xAA);
}


// Windows: the device descriptor, the address, the configuration and the strings, GET_MAX_LUN, the configuration, and the mounting reads.
void TestWindowsOrder() {
	Device fixture(MscTest::SmallVolume::Bytes);
	SIM_EXPECT_EQ(fixture.device.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);
	Address(fixture.sim, 5);
	Describe(fixture.sim);

	// The class requests of the interface reach the function only once the configuration selects it, so GET_MAX_LUN follows
	// SET_CONFIGURATION, as the Windows class driver sends it.
	Configure(fixture.sim);
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Configured);

	BotHost host(fixture.sim, fixture.device);
	uint8 lun = 0xFF;
	SIM_EXPECT_EQ(host.GetMaxLun(lun), ResultStatus::ok);
	SIM_EXPECT_EQ(lun, 0);

	// Windows asks TEST UNIT READY until the medium answers, and the medium is there.
	SIM_EXPECT_EQ(TestUnitReady(host, 1), ResultStatus::ok);
	MountVolume(host, 2);
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(MscTest::InAddress), 0);
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(MscTest::OutAddress), 0);
}


// Linux: the device descriptor, the address, GET_MAX_LUN before the configuration, and a first TEST UNIT READY and REQUEST SENSE
// before the mounting reads, as usb-storage sends them.
void TestLinuxOrder() {
	Device fixture(MscTest::SmallVolume::Bytes);
	SIM_EXPECT_EQ(fixture.device.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);
	fixture.sim.InjectBusReset();
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 6, 0x0100, 0x0000, 64), DeviceBytes);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 5, 9, 0x0000, 0)), ResultStatus::ok);
	Describe(fixture.sim);
	Configure(fixture.sim);

	BotHost host(fixture.sim, fixture.device);
	uint8 lun = 0xFF;
	SIM_EXPECT_EQ(host.GetMaxLun(lun), ResultStatus::ok);
	SIM_EXPECT_EQ(lun, 0);

	SIM_EXPECT_EQ(TestUnitReady(host, 1), ResultStatus::ok);
	std::array<uint8, 18> sense{};
	const std::array<uint8, 6> requestSenseCdb{ 0x03, 0x00, 0x00, 0x00, 18, 0x00 };
	ExpectCommandPasses(host, 2, requestSenseCdb, 18, true, sense);
	SIM_EXPECT_EQ(sense[2], 0x00);
	MountVolume(host, 3);
}


// macOS starts with the first 8 bytes of the device descriptor, and then the same mounting reads. The answer is cut to 8 bytes and no
// zero-length packet follows it.
void TestMacOrder() {
	Device fixture(MscTest::SmallVolume::Bytes);
	SIM_EXPECT_EQ(fixture.device.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);
	fixture.sim.InjectBusReset();
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x80, 6, 0x0100, 0x0000, 8), std::span<const uint8>(DeviceBytes).first(8));
	fixture.sim.InjectBusReset();
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 5, 3, 0x0000, 0)), ResultStatus::ok);
	Describe(fixture.sim);
	Configure(fixture.sim);

	BotHost host(fixture.sim, fixture.device);
	MountVolume(host, 1);
}


// The medium is not there when the device starts: TEST UNIT READY answers NOT READY, MEDIUM NOT PRESENT, and the host asks again. When
// the medium arrives, the next TEST UNIT READY passes and the volume mounts.
void TestTestUnitReadyLoopUntilMedium() {
	Device fixture(MscTest::SmallVolume::Bytes);
	SIM_EXPECT_EQ(fixture.device.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);
	fixture.disk.SetPresent(false);
	fixture.device.Process();
	Address(fixture.sim, 5);
	Describe(fixture.sim);
	Configure(fixture.sim);

	BotHost host(fixture.sim, fixture.device);
	uint32 tag = 1;
	for (uint32 attempt = 0; attempt < 2; attempt++) {
		Outcome outcome = host.Command(tag++, std::array<uint8, 6>{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, 0, false, {}, {});
		SIM_EXPECT_EQ(outcome.csw.status, MscStatusFailed);
		std::array<uint8, 18> sense{};
		const std::array<uint8, 6> requestSenseCdb{ 0x03, 0x00, 0x00, 0x00, 18, 0x00 };
		ExpectCommandPasses(host, tag++, requestSenseCdb, 18, true, sense);
		SIM_EXPECT_EQ(sense[2], 0x02);
		SIM_EXPECT_EQ(sense[12], 0x3A);
	}

	fixture.disk.SetPresent(true);
	fixture.device.Process();
	SIM_EXPECT_EQ(TestUnitReady(host, tag++), ResultStatus::ok);
	MountVolume(host, tag);
}


}


int main() {
	TestWindowsOrder();
	TestLinuxOrder();
	TestMacOrder();
	TestTestUnitReadyLoopUntilMedium();
	return SimTest::Finish("TestMscEnumeration");
}
