#include <VHAL.h>
#include "MscHost.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <array>
#include <cstddef>
#include <span>


// The Bulk-Only Transport against the host model: each of the thirteen host/device cases of BOT 1.0 section 6.7, the CBW validity
// rules, the reset recovery, and the commands the device refuses. A case checks what the host saw (the data, the CSW status and
// residue, the tag), what the device did to its pipes (the stall counts) and what reached the medium. The medium is a RAM disk of
// 128 blocks of 512 bytes, and the data buffer of the function is one block, so every transfer is one chunk or a few.


namespace {


using Device = MscTest::RamMscDevice<512, 128>;
using MscTest::BotHost;
using MscTest::Outcome;
using MscTest::HandCbw;
using MscTest::InAddress;
using MscTest::OutAddress;


constexpr uint32 Block = 512;
constexpr uint32 Tag = 0x12345678;


inline constexpr std::array<uint8, 6> TestUnitReady{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
inline constexpr std::array<uint8, 6> Inquiry{ 0x12, 0x00, 0x00, 0x00, 36, 0x00 };
inline constexpr std::array<uint8, 6> RequestSense{ 0x03, 0x00, 0x00, 0x00, 18, 0x00 };


// READ(10) and WRITE(10): opcode, the block address (big endian) and the transfer length in blocks (big endian).
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


inline constexpr std::array<uint8, 10> Read1 = Cdb10(0x28, 0, 1);
inline constexpr std::array<uint8, 10> Write1 = Cdb10(0x2A, 0, 1);
inline constexpr std::array<uint8, 10> Write2 = Cdb10(0x2A, 0, 2);


// Two blocks of data for the writes: byte n is 0x40 + n, so a block that was written can be told from one that was not.
constexpr std::array<uint8, 2 * Block> MakePattern() {
	std::array<uint8, 2 * Block> bytes{};
	for (std::size_t index = 0; index < bytes.size(); index++) {
		bytes[index] = static_cast<uint8>(0x40 + index);
	}

	return bytes;
}


inline constexpr std::array<uint8, 2 * Block> Pattern = MakePattern();


// The INQUIRY answer of the test identity (SPC-4 table 151): removable, SPC-2, response data format 2, 31 more bytes.
constexpr std::array<uint8, 36> ExpectedInquiry{
	0x00, 0x80, 0x04, 0x02, 0x1F, 0x00, 0x00, 0x00,
	'V', 'H', 'A', 'L', ' ', ' ', ' ', ' ',
	'R', 'A', 'M', ' ', 'D', 'i', 's', 'k', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
	'1', '.', '0', '0'
};


void Attach(Device& fixture) {
	SIM_EXPECT_EQ(fixture.Attach(), ResultStatus::ok);
}


bool AllZero(std::span<const uint8> bytes) {
	for (uint8 value : bytes) {
		if (value != 0) {
			return false;
		}
	}

	return true;
}


// The common checks of a command that completes: the CBW was taken, the CSW is valid with the tag, and the status and residue are as stated.
void ExpectCsw(const Outcome& outcome, uint32 tag, uint8 status, uint32 residue) {
	SIM_EXPECT_EQ(outcome.cbw, ResultStatus::ok);
	SIM_EXPECT_EQ(outcome.cswStatus, ResultStatus::ok);
	SIM_EXPECT(outcome.csw.valid);
	SIM_EXPECT_EQ(outcome.csw.tag, tag);
	SIM_EXPECT_EQ(outcome.csw.status, status);
	SIM_EXPECT_EQ(outcome.csw.residue, residue);
}


// Case 1, Hn = Dn: no data either way, and the command passes.
void CaseOneHnEqualsDn() {
	Device fixture;
	Attach(fixture);
	BotHost host(fixture.sim, fixture.device);

	Outcome outcome = host.Command(Tag, TestUnitReady, 0, false, {}, {});
	ExpectCsw(outcome, Tag, MscStatusPassed, 0);
	SIM_EXPECT_EQ(outcome.dataIn, 0);
	SIM_EXPECT_EQ(outcome.dataOut, 0);
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(InAddress), 0);
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(OutAddress), 0);
}


// Case 2, Hn < Di: the host expects no data, the device would send an answer. A phase error, and no pipe is stalled.
void CaseTwoHnLessThanDi() {
	Device fixture;
	Attach(fixture);
	BotHost host(fixture.sim, fixture.device);

	Outcome outcome = host.Command(Tag, Inquiry, 0, false, {}, {});
	ExpectCsw(outcome, Tag, MscStatusPhaseError, 0);
	SIM_EXPECT_EQ(outcome.dataIn, 0);
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(InAddress), 0);
}


// Case 3, Hn < Do: the host expects no data, the write would take some. A phase error, and the medium is not written.
void CaseThreeHnLessThanDo() {
	Device fixture;
	Attach(fixture);
	BotHost host(fixture.sim, fixture.device);

	Outcome outcome = host.Command(Tag, Write1, 0, false, {}, {});
	ExpectCsw(outcome, Tag, MscStatusPhaseError, 0);
	SIM_EXPECT_EQ(outcome.dataOut, 0);
	SIM_EXPECT(AllZero(fixture.disk.Storage()));
}


// Case 4, Hi > Dn: the host expects data, the device has none. The IN pipe is stalled, the host clears it, and the CSW reports
// the whole length as residue.
void CaseFourHiGreaterThanDn() {
	Device fixture;
	Attach(fixture);
	BotHost host(fixture.sim, fixture.device);

	std::array<uint8, 64> in{};
	Outcome outcome = host.Command(Tag, TestUnitReady, 36, true, {}, in);
	ExpectCsw(outcome, Tag, MscStatusPassed, 36);
	SIM_EXPECT_EQ(outcome.dataIn, 0);
	SIM_EXPECT(outcome.inStalled);
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(InAddress), 1);
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(OutAddress), 0);
}


// Case 5, Hi > Di: the device sends its 36 bytes, then stalls the IN pipe. The residue is the host's length minus what was sent.
void CaseFiveHiGreaterThanDi() {
	Device fixture;
	Attach(fixture);
	BotHost host(fixture.sim, fixture.device);

	std::array<uint8, 96> in{};
	Outcome outcome = host.Command(Tag, Inquiry, 96, true, {}, in);
	ExpectCsw(outcome, Tag, MscStatusPassed, 60);
	SIM_EXPECT_EQ(outcome.dataIn, 36);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(36), ExpectedInquiry));
	SIM_EXPECT(outcome.inStalled);
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(InAddress), 1);
}


// Case 6, Hi = Di: the host's length is the device's, so the data goes and the CSW has residue 0.
void CaseSixHiEqualsDi() {
	Device fixture;
	Attach(fixture);
	BotHost host(fixture.sim, fixture.device);

	std::array<uint8, 64> in{};
	Outcome outcome = host.Command(Tag, Inquiry, 36, true, {}, in);
	ExpectCsw(outcome, Tag, MscStatusPassed, 0);
	SIM_EXPECT_EQ(outcome.dataIn, 36);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(36), ExpectedInquiry));
	SIM_EXPECT(!outcome.inStalled);
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(InAddress), 0);
}


// Case 7, Hi < Di: the device sends the host's 16 bytes and reports a phase error. The pipe is not stalled.
void CaseSevenHiLessThanDi() {
	Device fixture;
	Attach(fixture);
	BotHost host(fixture.sim, fixture.device);

	std::array<uint8, 64> in{};
	Outcome outcome = host.Command(Tag, Inquiry, 16, true, {}, in);
	ExpectCsw(outcome, Tag, MscStatusPhaseError, 0);
	SIM_EXPECT_EQ(outcome.dataIn, 16);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(16), std::span<const uint8>(ExpectedInquiry).first(16)));
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(InAddress), 0);
}


// Case 8, Hi <> Do: the host expects to receive, the device intends to receive. A phase error, the IN pipe is stalled, and the
// medium is not written.
void CaseEightHiNotEqualDo() {
	Device fixture;
	Attach(fixture);
	BotHost host(fixture.sim, fixture.device);

	std::array<uint8, 64> in{};
	Outcome outcome = host.Command(Tag, Write1, Block, true, {}, in);
	ExpectCsw(outcome, Tag, MscStatusPhaseError, Block);
	SIM_EXPECT_EQ(outcome.dataIn, 0);
	SIM_EXPECT(outcome.inStalled);
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(InAddress), 1);
	SIM_EXPECT(AllZero(fixture.disk.Storage()));
}


// Case 9, Ho > Dn: the host sends data the device does not take. The OUT pipe is stalled, and the residue is the whole length.
void CaseNineHoGreaterThanDn() {
	Device fixture;
	Attach(fixture);
	BotHost host(fixture.sim, fixture.device);

	Outcome outcome = host.Command(Tag, TestUnitReady, Block, false, Pattern, {});
	ExpectCsw(outcome, Tag, MscStatusPassed, Block);
	SIM_EXPECT_EQ(outcome.dataOut, 0);
	SIM_EXPECT(outcome.outStalled);
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(OutAddress), 1);
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(InAddress), 0);
}


// Case 10, Ho <> Di: the host sends data where the device would send. A phase error, and the OUT pipe is stalled.
void CaseTenHoNotEqualDi() {
	Device fixture;
	Attach(fixture);
	BotHost host(fixture.sim, fixture.device);

	Outcome outcome = host.Command(Tag, Inquiry, 36, false, Pattern, {});
	ExpectCsw(outcome, Tag, MscStatusPhaseError, 36);
	SIM_EXPECT_EQ(outcome.dataOut, 0);
	SIM_EXPECT(outcome.outStalled);
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(OutAddress), 1);
}


// Case 11, Ho > Do: the host sends two blocks and the write takes one. The device stalls the OUT pipe after the block, the block is
// written in Process(), and the residue is the part of the host's length that was not taken.
void CaseElevenHoGreaterThanDo() {
	Device fixture;
	Attach(fixture);
	BotHost host(fixture.sim, fixture.device);

	Outcome outcome = host.Command(Tag, Write1, 2 * Block, false, Pattern, {});
	ExpectCsw(outcome, Tag, MscStatusPassed, Block);
	SIM_EXPECT_EQ(outcome.dataOut, Block);
	SIM_EXPECT(outcome.outStalled);
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(OutAddress), 1);
	SIM_EXPECT(SimHost::SameBytes(fixture.disk.Storage().first(Block), std::span<const uint8>(Pattern).first(Block)));
}


// Case 12, Ho = Do: the host sends one block and the write takes it. No pipe is stalled, and the residue is 0.
void CaseTwelveHoEqualsDo() {
	Device fixture;
	Attach(fixture);
	BotHost host(fixture.sim, fixture.device);

	Outcome outcome = host.Command(Tag, Write1, Block, false, Pattern, {});
	ExpectCsw(outcome, Tag, MscStatusPassed, 0);
	SIM_EXPECT_EQ(outcome.dataOut, Block);
	SIM_EXPECT(!outcome.outStalled);
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(OutAddress), 0);
	SIM_EXPECT(SimHost::SameBytes(fixture.disk.Storage().first(Block), std::span<const uint8>(Pattern).first(Block)));
}


// Case 13, Ho < Do: the host sends one block and the write would take two. A phase error, the OUT pipe is stalled, and the medium
// is not written.
void CaseThirteenHoLessThanDo() {
	Device fixture;
	Attach(fixture);
	BotHost host(fixture.sim, fixture.device);

	Outcome outcome = host.Command(Tag, Write2, Block, false, Pattern, {});
	ExpectCsw(outcome, Tag, MscStatusPhaseError, Block);
	SIM_EXPECT_EQ(outcome.dataOut, 0);
	SIM_EXPECT(outcome.outStalled);
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(OutAddress), 1);
	SIM_EXPECT(AllZero(fixture.disk.Storage()));
}


// A CBW with a bad signature stalls both pipes. The stall holds after the host clears it alone: only a Bulk-Only Mass Storage Reset
// returns the function to its idle stage.
void CaseInvalidSignatureStallsBothPipes() {
	Device fixture;
	Attach(fixture);
	BotHost host(fixture.sim, fixture.device);

	std::array<uint8, MscCbwLength> cbw = HandCbw(7, 0, 0x00, 0, 6, TestUnitReady);
	cbw[0] = 0x00;
	Outcome outcome = host.RawCbw(cbw);
	SIM_EXPECT_EQ(outcome.cbw, ResultStatus::ok);
	SIM_EXPECT(!outcome.csw.valid);
	SIM_EXPECT(fixture.sim.IsStalled(InAddress));
	SIM_EXPECT(fixture.sim.IsStalled(OutAddress));

	SIM_EXPECT_EQ(host.ClearHalt(OutAddress), ResultStatus::ok);
	SIM_EXPECT(fixture.sim.IsStalled(OutAddress));
	SIM_EXPECT_EQ(host.RawCbw(HandCbw(8, 0, 0x00, 0, 6, TestUnitReady)).cbw, ResultStatus::accessError);
}


// The reset recovery: the Bulk-Only Mass Storage Reset, then the clears of both pipes. The next command is taken and answered.
void CaseResetRecovery() {
	Device fixture;
	Attach(fixture);
	BotHost host(fixture.sim, fixture.device);

	std::array<uint8, MscCbwLength> cbw = HandCbw(7, 0, 0x00, 0, 6, TestUnitReady);
	cbw[0] = 0x00;
	host.RawCbw(cbw);

	SIM_EXPECT_EQ(host.Reset(), ResultStatus::ok);
	SIM_EXPECT_EQ(host.ClearHalt(InAddress), ResultStatus::ok);
	SIM_EXPECT_EQ(host.ClearHalt(OutAddress), ResultStatus::ok);

	Outcome outcome = host.Command(Tag, TestUnitReady, 0, false, {}, {});
	ExpectCsw(outcome, Tag, MscStatusPassed, 0);
}


// CBWs of the wrong length, 30 bytes, 32 bytes and a full 64-byte packet, each stall both pipes.
void CaseInvalidLengthStallsBothPipes() {
	const std::array<std::size_t, 3> lengths{ 30, 32, 64 };
	for (std::size_t length : lengths) {
		Device fixture;
		Attach(fixture);
		BotHost host(fixture.sim, fixture.device);

		std::array<uint8, 64> cbw{};
		std::array<uint8, MscCbwLength> valid = HandCbw(3, 0, 0x00, 0, 6, TestUnitReady);
		for (std::size_t index = 0; index < valid.size(); index++) {
			cbw[index] = valid[index];
		}

		Outcome outcome = host.RawCbw(std::span<const uint8>(cbw).first(length));
		SIM_EXPECT_EQ(outcome.cbw, ResultStatus::ok);
		SIM_EXPECT(!outcome.csw.valid);
		SIM_EXPECT(fixture.sim.IsStalled(InAddress));
		SIM_EXPECT(fixture.sim.IsStalled(OutAddress));
	}
}


// A reset while the device waits for the host to read: the armed IN transfer is abandoned, and the next command is answered from its
// own CBW.
void CaseResetWhileDataIsArmed() {
	Device fixture;
	Attach(fixture);
	BotHost host(fixture.sim, fixture.device);

	// The CBW goes in, and Process() reads the first block into the buffer and arms the IN endpoint. The host does not read it.
	std::array<uint8, MscCbwLength> cbw = HandCbw(4, 2 * Block, MscFlagDataIn, 0, 10, Read1);
	SIM_EXPECT(fixture.sim.HostOut(OutAddress, cbw).IsOk());
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(InAddress));

	SIM_EXPECT_EQ(host.Reset(), ResultStatus::ok);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(InAddress));
	SIM_EXPECT(fixture.sim.IsStalled(InAddress));
	SIM_EXPECT_EQ(host.ClearHalt(InAddress), ResultStatus::ok);
	SIM_EXPECT_EQ(host.ClearHalt(OutAddress), ResultStatus::ok);

	Outcome outcome = host.Command(Tag, TestUnitReady, 0, false, {}, {});
	ExpectCsw(outcome, Tag, MscStatusPassed, 0);
}


// An opcode the function does not answer: CHECK CONDITION, ILLEGAL REQUEST with INVALID COMMAND OPERATION CODE. REQUEST SENSE then
// reports it.
void CaseUnknownOpcode() {
	Device fixture;
	Attach(fixture);
	BotHost host(fixture.sim, fixture.device);

	std::array<uint8, 6> unknown{ 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00 };
	Outcome outcome = host.Command(Tag, unknown, 0, false, {}, {});
	ExpectCsw(outcome, Tag, MscStatusFailed, 0);

	std::array<uint8, 18> sense{};
	Outcome reply = host.Command(Tag + 1, RequestSense, 18, true, {}, sense);
	ExpectCsw(reply, Tag + 1, MscStatusPassed, 0);
	SIM_EXPECT_EQ(sense[2], 0x05);
	SIM_EXPECT_EQ(sense[12], 0x20);
	SIM_EXPECT_EQ(sense[13], 0x00);
}


// A CBW that is not meaningful (another LUN, a reserved flag bit, a command length of 0) fails as a command with no data of the device's
// own. The residue is the host's whole length, and a read in such a CBW stalls its IN pipe, so the CSW comes after the clear.
void CaseNotMeaningfulCbwFails() {
	Device fixture;
	Attach(fixture);
	BotHost host(fixture.sim, fixture.device);

	Outcome lun = host.RawCbw(HandCbw(11, 0, 0x00, 1, 6, TestUnitReady));
	SIM_EXPECT_EQ(lun.cbw, ResultStatus::ok);
	SIM_EXPECT(lun.csw.valid);
	SIM_EXPECT_EQ(lun.csw.status, MscStatusFailed);
	SIM_EXPECT_EQ(lun.csw.residue, 0);

	Outcome reserved = host.RawCbw(HandCbw(12, 0, 0x40, 0, 6, TestUnitReady));
	SIM_EXPECT(reserved.csw.valid);
	SIM_EXPECT_EQ(reserved.csw.status, MscStatusFailed);

	Outcome noCommand = host.RawCbw(HandCbw(13, 0, 0x00, 0, 0, TestUnitReady));
	SIM_EXPECT(noCommand.csw.valid);
	SIM_EXPECT_EQ(noCommand.csw.status, MscStatusFailed);

	Outcome read = host.RawCbw(HandCbw(14, Block, MscFlagDataIn, 1, 10, Read1));
	SIM_EXPECT_EQ(read.cbw, ResultStatus::ok);
	SIM_EXPECT(read.csw.valid);
	SIM_EXPECT_EQ(read.csw.status, MscStatusFailed);
	SIM_EXPECT_EQ(read.csw.residue, Block);
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(InAddress), 1);
}


}


int main() {
	CaseOneHnEqualsDn();
	CaseTwoHnLessThanDi();
	CaseThreeHnLessThanDo();
	CaseFourHiGreaterThanDn();
	CaseFiveHiGreaterThanDi();
	CaseSixHiEqualsDi();
	CaseSevenHiLessThanDi();
	CaseEightHiNotEqualDo();
	CaseNineHoGreaterThanDn();
	CaseTenHoNotEqualDi();
	CaseElevenHoGreaterThanDo();
	CaseTwelveHoEqualsDo();
	CaseThirteenHoLessThanDo();
	CaseInvalidSignatureStallsBothPipes();
	CaseResetRecovery();
	CaseInvalidLengthStallsBothPipes();
	CaseResetWhileDataIsArmed();
	CaseUnknownOpcode();
	CaseNotMeaningfulCbwFails();
	return SimTest::Finish("TestMscBot");
}
