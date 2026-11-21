#include <VHAL.h>
#include "MscHost.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <array>
#include <cstddef>
#include <span>


// The SCSI commands of the mass storage function, each against its answer, through the Bulk-Only host model. The tests check the
// bytes of the answers, the CSW of each command, the sense data that REQUEST SENSE reports, and the medium state the commands change:
// presence, write protection and eject. The sense data of an error is kept until the next command, except that REQUEST SENSE itself
// does not clear it.


namespace {


using Device = MscTest::RamMscDevice<512, 128>;
using MscTest::BotHost;
using MscTest::Outcome;


// The commands as their CDBs are written in the SCSI standards.
inline constexpr std::array<uint8, 6> TestUnitReadyCdb{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
inline constexpr std::array<uint8, 6> RequestSenseCdb{ 0x03, 0x00, 0x00, 0x00, 18, 0x00 };
inline constexpr std::array<uint8, 6> InquiryCdb{ 0x12, 0x00, 0x00, 0x00, 36, 0x00 };
inline constexpr std::array<uint8, 10> ReadCapacityCdb{ 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
inline constexpr std::array<uint8, 10> ReadFormatCapacitiesCdb{ 0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 12, 0x00 };
inline constexpr std::array<uint8, 6> ModeSense6Cdb{ 0x1A, 0x00, 0x3F, 0x00, 4, 0x00 };
inline constexpr std::array<uint8, 10> ModeSense10Cdb{ 0x5A, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 8, 0x00 };
inline constexpr std::array<uint8, 6> PreventCdb{ 0x1E, 0x00, 0x00, 0x00, 0x01, 0x00 };
inline constexpr std::array<uint8, 6> AllowCdb{ 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00 };
inline constexpr std::array<uint8, 6> EjectCdb{ 0x1B, 0x00, 0x00, 0x00, 0x02, 0x00 };
inline constexpr std::array<uint8, 6> StartCdb{ 0x1B, 0x00, 0x00, 0x00, 0x01, 0x00 };
inline constexpr std::array<uint8, 6> StopCdb{ 0x1B, 0x00, 0x00, 0x00, 0x00, 0x00 };
inline constexpr std::array<uint8, 6> LoadCdb{ 0x1B, 0x00, 0x00, 0x00, 0x03, 0x00 };
inline constexpr std::array<uint8, 10> SyncCdb{ 0x35, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };


// READ(10), WRITE(10) and VERIFY(10): the block address and the length in blocks, big endian.
constexpr std::array<uint8, 10> Cdb10(uint8 opcode, uint32 lba, uint16 count) {
	std::array<uint8, 10> cdb{};
	cdb[0] = opcode;
	cdb[2] = static_cast<uint8>((lba >> 24) & 0xFF);
	cdb[3] = static_cast<uint8>((lba >> 16) & 0xFF);
	cdb[4] = static_cast<uint8>((lba >> 8) & 0xFF);
	cdb[5] = static_cast<uint8>(lba & 0xFF);
	cdb[7] = static_cast<uint8>((count >> 8) & 0xFF);
	cdb[8] = static_cast<uint8>(count & 0xFF);
	return cdb;
}


// The INQUIRY answer of the test identity (SPC-4 table 151).
constexpr std::array<uint8, 36> InquiryAnswer{
	0x00, 0x80, 0x04, 0x02, 0x1F, 0x00, 0x00, 0x00,
	'V', 'H', 'A', 'L', ' ', ' ', ' ', ' ',
	'R', 'A', 'M', ' ', 'D', 'i', 's', 'k', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
	'1', '.', '0', '0'
};


// A device and its host. Each command gets the next tag, so the CSW of each one is checked against its own.
struct Session {
	Device fixture;
	BotHost host;
	uint32 tag = 0x100;


	explicit Session(std::span<const uint8> image = std::span<const uint8>(), const MscIdentity& identity = MscTest::Identity)
		: fixture(image, identity),
		  host(fixture.sim, fixture.device) {
	}


	void Attach() {
		SIM_EXPECT_EQ(fixture.Attach(), ResultStatus::ok);
	}


	Outcome Run(std::span<const uint8> cdb, uint32 length, bool dataIn, std::span<const uint8> out, std::span<uint8> in) {
		tag++;
		return host.Command(tag, cdb, length, dataIn, out, in);
	}


	// Runs a command with no data, and checks that its CSW passes.
	void Pass(std::span<const uint8> cdb) {
		Outcome outcome = Run(cdb, 0, false, {}, {});
		SIM_EXPECT_EQ(outcome.cswStatus, ResultStatus::ok);
		SIM_EXPECT_EQ(outcome.csw.status, MscStatusPassed);
	}


	// Runs a command with no data, and checks that its CSW fails.
	void Fail(std::span<const uint8> cdb) {
		Outcome outcome = Run(cdb, 0, false, {}, {});
		SIM_EXPECT_EQ(outcome.cswStatus, ResultStatus::ok);
		SIM_EXPECT_EQ(outcome.csw.status, MscStatusFailed);
	}


	// REQUEST SENSE, whole. Returns the 18 bytes it answered with.
	std::array<uint8, 18> Sense() {
		std::array<uint8, 18> sense{};
		Outcome outcome = Run(RequestSenseCdb, 18, true, {}, sense);
		SIM_EXPECT_EQ(outcome.csw.status, MscStatusPassed);
		SIM_EXPECT_EQ(outcome.dataIn, 18);
		return sense;
	}


	// The medium changed outside the host (a card inserted or removed): the device reads it at its next Process().
	void MediumChanged(bool present) {
		fixture.disk.SetPresent(present);
		fixture.device.Process();
	}
};


void ExpectSense(const std::array<uint8, 18>& sense, uint8 key, uint8 asc, uint8 ascq) {
	SIM_EXPECT_EQ(sense[0], 0x70);
	SIM_EXPECT_EQ(sense[2], key);
	SIM_EXPECT_EQ(sense[7], 10);
	SIM_EXPECT_EQ(sense[12], asc);
	SIM_EXPECT_EQ(sense[13], ascq);
}


void TestUnitReadyAnswers() {
	Session session;
	session.Attach();

	session.Pass(TestUnitReadyCdb);

	// Not present: the command fails with NOT READY, MEDIUM NOT PRESENT, and a later command with a medium clears it.
	session.MediumChanged(false);
	session.Fail(TestUnitReadyCdb);
	ExpectSense(session.Sense(), 0x02, 0x3A, 0x00);

	session.MediumChanged(true);
	session.Pass(TestUnitReadyCdb);
	ExpectSense(session.Sense(), 0x00, 0x00, 0x00);
}


void RequestSenseKeepsTheErrorUntilTheNextCommand() {
	Session session;
	session.Attach();

	// An unknown opcode leaves ILLEGAL REQUEST with INVALID COMMAND OPERATION CODE. Two REQUEST SENSE commands both report it.
	std::array<uint8, 6> unknown{ 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00 };
	session.Fail(unknown);
	ExpectSense(session.Sense(), 0x05, 0x20, 0x00);
	ExpectSense(session.Sense(), 0x05, 0x20, 0x00);

	// REQUEST SENSE with a shorter allocation length answers with that many bytes.
	std::array<uint8, 4> sense{};
	std::array<uint8, 6> shortSense{ 0x03, 0x00, 0x00, 0x00, 4, 0x00 };
	Outcome outcome = session.Run(shortSense, 4, true, {}, sense);
	SIM_EXPECT_EQ(outcome.dataIn, 4);
	SIM_EXPECT_EQ(sense[2], 0x05);

	// The next command that is not REQUEST SENSE clears the error.
	session.Pass(TestUnitReadyCdb);
	ExpectSense(session.Sense(), 0x00, 0x00, 0x00);
}


void InquiryAnswers() {
	Session session;
	session.Attach();

	std::array<uint8, 36> answer{};
	Outcome outcome = session.Run(InquiryCdb, 36, true, {}, answer);
	SIM_EXPECT_EQ(outcome.csw.status, MscStatusPassed);
	SIM_EXPECT_EQ(outcome.csw.residue, 0);
	SIM_EXPECT(SimHost::SameBytes(answer, InquiryAnswer));

	// The vendor identity is cut to 8 characters, the product to 16 and the revision to 4 on the way out.
	constexpr MscIdentity longIdentity{ "VENDOR-NAME-TOO-LONG", "PRODUCT-NAME-IS-LONGER-THAN-16", "REVISION-5", false };
	Session other(std::span<const uint8>(), longIdentity);
	other.Attach();
	std::array<uint8, 36> cut{};
	Outcome cutOutcome = other.Run(InquiryCdb, 36, true, {}, cut);
	SIM_EXPECT_EQ(cutOutcome.csw.status, MscStatusPassed);
	SIM_EXPECT_EQ(cut[1], 0x00);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(cut).subspan(8, 8), std::span<const uint8>(std::array<uint8, 8>{ 'V', 'E', 'N', 'D', 'O', 'R', '-', 'N' })));
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(cut).subspan(16, 16), std::span<const uint8>(std::array<uint8, 16>{ 'P', 'R', 'O', 'D', 'U', 'C', 'T', '-', 'N', 'A', 'M', 'E', '-', 'I', 'S', '-' })));
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(cut).subspan(32, 4), std::span<const uint8>(std::array<uint8, 4>{ 'R', 'E', 'V', 'I' })));

	// An allocation shorter than the answer gets the first bytes of it.
	std::array<uint8, 5> few{};
	std::array<uint8, 6> shortInquiry{ 0x12, 0x00, 0x00, 0x00, 5, 0x00 };
	Outcome fewOutcome = session.Run(shortInquiry, 5, true, {}, few);
	SIM_EXPECT_EQ(fewOutcome.dataIn, 5);
	SIM_EXPECT_EQ(fewOutcome.csw.status, MscStatusPassed);

	// EVPD asks for a VPD page this function does not have: an invalid field in the CDB.
	std::array<uint8, 6> evpd{ 0x12, 0x01, 0x80, 0x00, 36, 0x00 };
	std::array<uint8, 36> ignored{};
	Outcome evpdOutcome = session.Run(evpd, 36, true, {}, ignored);
	SIM_EXPECT_EQ(evpdOutcome.csw.status, MscStatusFailed);
	ExpectSense(session.Sense(), 0x05, 0x24, 0x00);
}


void ReadCapacityAnswers() {
	Session session;
	session.Attach();

	// A 128-block medium of 512-byte blocks: the last block is 127 and the block length is 512, both big endian.
	std::array<uint8, 8> capacity{};
	Outcome outcome = session.Run(ReadCapacityCdb, 8, true, {}, capacity);
	SIM_EXPECT_EQ(outcome.csw.status, MscStatusPassed);
	SIM_EXPECT_EQ(capacity[0], 0x00);
	SIM_EXPECT_EQ(capacity[1], 0x00);
	SIM_EXPECT_EQ(capacity[2], 0x00);
	SIM_EXPECT_EQ(capacity[3], 0x7F);
	SIM_EXPECT_EQ(capacity[4], 0x00);
	SIM_EXPECT_EQ(capacity[5], 0x00);
	SIM_EXPECT_EQ(capacity[6], 0x02);
	SIM_EXPECT_EQ(capacity[7], 0x00);

	session.MediumChanged(false);
	Outcome absent = session.Run(ReadCapacityCdb, 8, true, {}, capacity);
	SIM_EXPECT_EQ(absent.csw.status, MscStatusFailed);
	ExpectSense(session.Sense(), 0x02, 0x3A, 0x00);
}


void ReadFormatCapacitiesAnswers() {
	Session session;
	session.Attach();

	// One descriptor: 128 blocks, formatted media (code 2), 512-byte blocks in three bytes.
	std::array<uint8, 12> present{};
	Outcome outcome = session.Run(ReadFormatCapacitiesCdb, 12, true, {}, present);
	SIM_EXPECT_EQ(outcome.csw.status, MscStatusPassed);
	constexpr std::array<uint8, 12> expectedPresent{ 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x80, 0x02, 0x00, 0x02, 0x00 };
	SIM_EXPECT(SimHost::SameBytes(present, expectedPresent));

	// No medium: the code says so (3), and the block count is 0. The block length is still reported.
	session.MediumChanged(false);
	std::array<uint8, 12> absent{};
	Outcome absentOutcome = session.Run(ReadFormatCapacitiesCdb, 12, true, {}, absent);
	SIM_EXPECT_EQ(absentOutcome.csw.status, MscStatusPassed);
	constexpr std::array<uint8, 12> expectedAbsent{ 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x02, 0x00 };
	SIM_EXPECT(SimHost::SameBytes(absent, expectedAbsent));
}


void ModeSenseAnswers() {
	Session session;
	session.Attach();

	// The header alone, with the write-protect bit clear for a writable medium.
	std::array<uint8, 4> header6{};
	Outcome six = session.Run(ModeSense6Cdb, 4, true, {}, header6);
	SIM_EXPECT_EQ(six.csw.status, MscStatusPassed);
	constexpr std::array<uint8, 4> expectedSix{ 0x03, 0x00, 0x00, 0x00 };
	SIM_EXPECT(SimHost::SameBytes(header6, expectedSix));

	std::array<uint8, 8> header10{};
	Outcome ten = session.Run(ModeSense10Cdb, 8, true, {}, header10);
	SIM_EXPECT_EQ(ten.csw.status, MscStatusPassed);
	constexpr std::array<uint8, 8> expectedTen{ 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	SIM_EXPECT(SimHost::SameBytes(header10, expectedTen));

	// A write-protected medium sets the write-protect bit (the device-specific byte) in both headers.
	session.fixture.disk.SetReadOnly(true);
	session.fixture.device.Process();
	Outcome protectedSix = session.Run(ModeSense6Cdb, 4, true, {}, header6);
	SIM_EXPECT_EQ(protectedSix.csw.status, MscStatusPassed);
	SIM_EXPECT_EQ(header6[2], 0x80);

	Outcome protectedTen = session.Run(ModeSense10Cdb, 8, true, {}, header10);
	SIM_EXPECT_EQ(protectedTen.csw.status, MscStatusPassed);
	SIM_EXPECT_EQ(header10[3], 0x80);

	// A page the function does not have is an invalid field.
	std::array<uint8, 6> caching{ 0x1A, 0x00, 0x08, 0x00, 24, 0x00 };
	std::array<uint8, 24> ignored{};
	Outcome other = session.Run(caching, 24, true, {}, ignored);
	SIM_EXPECT_EQ(other.csw.status, MscStatusFailed);
	ExpectSense(session.Sense(), 0x05, 0x24, 0x00);
}


// The eject calls the delegate once, and the medium is not present afterwards. A prevented removal refuses the eject, and no
// delegate call follows.
struct EjectRecorder {
	uint32 count = 0;
	RamBlockDevice<512, 128>* disk = nullptr;


	void OnEject() {
		count++;
		disk->SetPresent(false);
	}
};


void EjectAndPreventRemoval() {
	Session session;
	EjectRecorder recorder;
	recorder.disk = &session.fixture.disk;
	SIM_EXPECT_EQ(session.fixture.function.SetEjectDelegate(MscFunction<512>::EjectDelegate::Bind<EjectRecorder, &EjectRecorder::OnEject>(&recorder)), ResultStatus::ok);
	session.Attach();

	// Prevent: the eject is refused with MEDIUM REMOVAL PREVENTED, and the medium stays.
	session.Pass(PreventCdb);
	session.Fail(EjectCdb);
	ExpectSense(session.Sense(), 0x05, 0x53, 0x02);
	SIM_EXPECT_EQ(recorder.count, 0);
	session.Pass(TestUnitReadyCdb);

	// Allow: the eject reaches the delegate, the delegate removes the medium, and the device reports it not present.
	session.Pass(AllowCdb);
	session.Pass(EjectCdb);
	SIM_EXPECT_EQ(recorder.count, 1);
	SIM_EXPECT(session.fixture.disk.IsPresent() == ResultStatus::notExist);
	session.Fail(TestUnitReadyCdb);
	ExpectSense(session.Sense(), 0x02, 0x3A, 0x00);

	// Start and load are not an eject, and they never reach the delegate.
	session.MediumChanged(true);
	session.Pass(StartCdb);
	session.Pass(StopCdb);
	session.Pass(LoadCdb);
	SIM_EXPECT_EQ(recorder.count, 1);
	session.Pass(TestUnitReadyCdb);
}


// The presence sample and an eject overlap. While the function samples the medium's presence, the host's START STOP UNIT ejects the
// medium from inside IsPresent(). The sample answered "present" before the eject, so it must not overwrite the eject's "not present":
// the next TEST UNIT READY fails with MEDIUM NOT PRESENT.
void EjectDuringPresenceSampleStays() {
	MscTest::MscDevice<512, MscTest::HookedDisk<512, 128>> fixture;
	BotHost host(fixture.sim, fixture.device);
	EjectRecorder recorder;
	recorder.disk = &fixture.disk;
	SIM_EXPECT_EQ(fixture.function.SetEjectDelegate(MscFunction<512>::EjectDelegate::Bind<EjectRecorder, &EjectRecorder::OnEject>(&recorder)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.Attach(), ResultStatus::ok);

	uint32 tag = 0x700;
	const std::array<uint8, 6> stopAndEject{ 0x1B, 0x00, 0x00, 0x00, 0x02, 0x00 };
	fixture.disk.onPresent = [&]() {
		tag++;
		Outcome eject = host.Command(tag, stopAndEject, 0, false, {}, {});
		SIM_EXPECT_EQ(eject.csw.status, MscStatusPassed);
	};

	fixture.device.Process();
	SIM_EXPECT_EQ(recorder.count, 1);

	Outcome testUnitReady = host.Command(++tag, TestUnitReadyCdb, 0, false, {}, {});
	SIM_EXPECT_EQ(testUnitReady.csw.status, MscStatusFailed);

	std::array<uint8, 18> sense{};
	Outcome reply = host.Command(++tag, RequestSenseCdb, 18, true, {}, sense);
	SIM_EXPECT_EQ(reply.csw.status, MscStatusPassed);
	ExpectSense(sense, 0x02, 0x3A, 0x00);
}


void VerifyAndSynchronize() {
	Session session;
	session.Attach();

	// VERIFY of four blocks reads them, and passes with no data for the host.
	std::array<uint8, 10> verify = Cdb10(0x2F, 0, 4);
	session.Pass(verify);

	// A range past the end is an LBA out of range.
	std::array<uint8, 10> beyond = Cdb10(0x2F, 126, 4);
	session.Fail(beyond);
	ExpectSense(session.Sense(), 0x05, 0x21, 0x00);

	// The byte-compare form is not supported.
	std::array<uint8, 10> compare = Cdb10(0x2F, 0, 1);
	compare[1] = 0x02;
	session.Fail(compare);
	ExpectSense(session.Sense(), 0x05, 0x24, 0x00);

	session.Pass(SyncCdb);

	session.MediumChanged(false);
	session.Fail(SyncCdb);
	ExpectSense(session.Sense(), 0x02, 0x3A, 0x00);
}


void ReadBeyondTheEndIsRefused() {
	Session session;
	session.Attach();

	// READ(10) from block 127 for two blocks: block 128 does not exist. The host expects 1024 bytes, the device has none, so the IN
	// pipe is stalled and the CSW reports the whole length as residue.
	std::array<uint8, 10> read = Cdb10(0x28, 127, 2);
	std::array<uint8, 64> in{};
	Outcome outcome = session.Run(read, 1024, true, {}, in);
	SIM_EXPECT_EQ(outcome.csw.status, MscStatusFailed);
	SIM_EXPECT_EQ(outcome.csw.residue, 1024);
	SIM_EXPECT(outcome.inStalled);
	ExpectSense(session.Sense(), 0x05, 0x21, 0x00);
}


}


int main() {
	TestUnitReadyAnswers();
	RequestSenseKeepsTheErrorUntilTheNextCommand();
	InquiryAnswers();
	ReadCapacityAnswers();
	ReadFormatCapacitiesAnswers();
	ModeSenseAnswers();
	EjectAndPreventRemoval();
	EjectDuringPresenceSampleStays();
	VerifyAndSynchronize();
	ReadBeyondTheEndIsRefused();
	return SimTest::Finish("TestMscScsi");
}
