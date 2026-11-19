#include <VHAL.h>
#include "MscHost.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <array>
#include <cstddef>
#include <span>


// The medium work of the mass storage function: reads and writes across the buffer boundaries, the cases of a partial transfer,
// the NAK the endpoints give while Process() works on the medium, write protection, and the errors of the medium in the middle of a
// transfer. The disk of these tests is a RAM disk with a fault switch: a read or write of a chosen block fails.


namespace {


using Disk = RamBlockDevice<512, 128>;
using MscTest::BotHost;
using MscTest::Outcome;
using MscTest::InAddress;
using MscTest::OutAddress;


constexpr uint32 Block = 512;


// A RAM disk that fails the access to one block on request, as a flash or a card can.
class FaultyDisk : public Disk {
public:
	using Disk::Disk;


	static constexpr uint32 NoFault = 0xFFFFFFFF;


	uint32 failRead = NoFault;
	uint32 failWrite = NoFault;


	ResultStatus Read(uint32 firstBlock, std::span<uint8> data) override {
		if (Touches(firstBlock, data.size(), failRead)) {
			return ResultStatus::readError;
		}

		return Disk::Read(firstBlock, data);
	}


	ResultStatus Write(uint32 firstBlock, std::span<const uint8> data) override {
		if (Touches(firstBlock, data.size(), failWrite)) {
			return ResultStatus::writeError;
		}

		return Disk::Write(firstBlock, data);
	}


private:
	static bool Touches(uint32 firstBlock, std::size_t bytes, uint32 block) {
		if (block == NoFault) {
			return false;
		}

		uint32 count = static_cast<uint32>(bytes / Block);
		return block >= firstBlock && block < firstBlock + count;
	}
};


// A device with a buffer of BufferSize bytes, a disk of type TDisk, and a host. Each command gets the next tag.
template<std::size_t BufferSize, typename TDisk>
struct Session {
	MscTest::MscDevice<BufferSize, TDisk> fixture;
	BotHost host;
	uint32 tag = 0x500;


	explicit Session(std::span<const uint8> image = std::span<const uint8>())
		: fixture(image),
		  host(fixture.sim, fixture.device) {
		SIM_EXPECT_EQ(fixture.Attach(), ResultStatus::ok);
	}


	Outcome Run(std::span<const uint8> cdb, uint32 length, bool dataIn, std::span<const uint8> out, std::span<uint8> in) {
		tag++;
		return host.Command(tag, cdb, length, dataIn, out, in);
	}


	// REQUEST SENSE, whole: the key, ASC and ASCQ of the last error.
	std::array<uint8, 18> Sense() {
		std::array<uint8, 18> sense{};
		const std::array<uint8, 6> cdb{ 0x03, 0x00, 0x00, 0x00, 18, 0x00 };
		Run(cdb, 18, true, {}, sense);
		return sense;
	}
};


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


// A pattern of n bytes, each byte a function of its position, so a block in the wrong place shows.
template<std::size_t Count>
constexpr std::array<uint8, Count> MakePattern(uint32 seed) {
	std::array<uint8, Count> bytes{};
	for (std::size_t index = 0; index < Count; index++) {
		bytes[index] = static_cast<uint8>((index * 31 + index / 256 * 7 + seed) & 0xFF);
	}

	return bytes;
}


inline constexpr std::array<uint8, 5 * Block> FiveBlocks = MakePattern<5 * Block>(3);
inline constexpr std::array<uint8, 10 * Block> TenBlocks = MakePattern<10 * Block>(9);


bool Same(std::span<const uint8> actual, std::span<const uint8> expected) {
	return SimHost::SameBytes(actual, expected);
}


// True when the first count blocks of the storage are all zero.
bool AllZeroBlocks(std::span<const uint8> storage, std::size_t count) {
	for (std::size_t index = 0; index < count * Block; index++) {
		if (storage[index] != 0) {
			return false;
		}
	}

	return true;
}


void ExpectSense(const std::array<uint8, 18>& sense, uint8 key, uint8 asc, uint8 ascq) {
	SIM_EXPECT_EQ(sense[2], key);
	SIM_EXPECT_EQ(sense[12], asc);
	SIM_EXPECT_EQ(sense[13], ascq);
}


// Five blocks written across five chunks of one block, then read back over the same chunks.
void RoundTripAcrossBlockChunks() {
	Session<Block, Disk> session;
	std::array<uint8, 10> write = Cdb10(0x2A, 4, 5);
	Outcome written = session.Run(write, 5 * Block, false, FiveBlocks, {});
	SIM_EXPECT_EQ(written.csw.status, MscStatusPassed);
	SIM_EXPECT_EQ(written.csw.residue, 0);
	SIM_EXPECT_EQ(written.dataOut, 5 * Block);
	SIM_EXPECT(Same(session.fixture.disk.Storage().subspan(4 * Block, 5 * Block), FiveBlocks));

	std::array<uint8, 10> read = Cdb10(0x28, 4, 5);
	std::array<uint8, 5 * Block> back{};
	Outcome reread = session.Run(read, 5 * Block, true, {}, back);
	SIM_EXPECT_EQ(reread.csw.status, MscStatusPassed);
	SIM_EXPECT_EQ(reread.dataIn, 5 * Block);
	SIM_EXPECT(Same(back, FiveBlocks));
}


// Ten blocks over a 4096-byte buffer: a full chunk of eight blocks, then two. The boundary between them is inside the transfer.
void RoundTripAcrossLargeChunks() {
	Session<4096, Disk> session;
	std::array<uint8, 10> write = Cdb10(0x2A, 0, 10);
	Outcome written = session.Run(write, 10 * Block, false, TenBlocks, {});
	SIM_EXPECT_EQ(written.csw.status, MscStatusPassed);
	SIM_EXPECT_EQ(written.dataOut, 10 * Block);
	SIM_EXPECT(Same(session.fixture.disk.Storage().first(10 * Block), TenBlocks));

	std::array<uint8, 10> read = Cdb10(0x28, 0, 10);
	std::array<uint8, 10 * Block> back{};
	Outcome reread = session.Run(read, 10 * Block, true, {}, back);
	SIM_EXPECT_EQ(reread.csw.status, MscStatusPassed);
	SIM_EXPECT(Same(back, TenBlocks));
}


// The host expects 600 bytes of a two-block read (case 7 of the medium): the device sends the 600 bytes, cut in the middle of a
// block, and reports a phase error with no residue.
void ReadShorterThanTheDeviceHas() {
	Session<Block, Disk> session;
	std::array<uint8, 2 * Block> stored = MakePattern<2 * Block>(5);
	session.fixture.disk.Write(0, stored);

	std::array<uint8, 10> read = Cdb10(0x28, 0, 2);
	std::array<uint8, 2 * Block> back{};
	Outcome outcome = session.Run(read, 600, true, {}, back);
	SIM_EXPECT_EQ(outcome.csw.status, MscStatusPhaseError);
	SIM_EXPECT_EQ(outcome.csw.residue, 0);
	SIM_EXPECT_EQ(outcome.dataIn, 600);
	SIM_EXPECT(Same(std::span<const uint8>(back).first(600), std::span<const uint8>(stored).first(600)));
}


// The host expects 1024 bytes of a one-block read (case 5): the device sends its 512 bytes and stalls the IN pipe. The residue
// is the 512 bytes the device did not send.
void ReadLongerThanTheDeviceHas() {
	Session<Block, Disk> session;
	std::array<uint8, Block> stored = MakePattern<Block>(7);
	session.fixture.disk.Write(3, stored);

	std::array<uint8, 10> read = Cdb10(0x28, 3, 1);
	std::array<uint8, 2 * Block> back{};
	Outcome outcome = session.Run(read, 2 * Block, true, {}, back);
	SIM_EXPECT_EQ(outcome.csw.status, MscStatusPassed);
	SIM_EXPECT_EQ(outcome.csw.residue, Block);
	SIM_EXPECT_EQ(outcome.dataIn, Block);
	SIM_EXPECT(outcome.inStalled);
	SIM_EXPECT(Same(std::span<const uint8>(back).first(Block), stored));
}


// The endpoints NAK while Process() reads the medium. The CBW is taken, and the IN endpoint has nothing armed until Process() has read
// the block. Then the block is there and the CSW follows the data.
void InNaksWhileProcessReads() {
	Session<Block, Disk> session;
	std::array<uint8, Block> stored = MakePattern<Block>(11);
	session.fixture.disk.Write(0, stored);

	const std::array<uint8, 10> read = Cdb10(0x28, 0, 1);
	std::array<uint8, MscCbwLength> cbw = BotHost::MakeCbw(77, Block, true, read);
	SIM_EXPECT(session.fixture.sim.HostOut(OutAddress, cbw).IsOk());
	SIM_EXPECT(!session.fixture.sim.HasArmedTransfer(InAddress));

	std::array<uint8, Block> back{};
	uint32 count = 0;
	SIM_EXPECT_EQ(session.fixture.sim.HostIn(InAddress, back, count), ResultStatus::notReady);

	session.fixture.device.Process();
	SIM_EXPECT(session.fixture.sim.HasArmedTransfer(InAddress));
	SIM_EXPECT_EQ(session.fixture.sim.HostIn(InAddress, back, count), ResultStatus::ok);
	SIM_EXPECT_EQ(count, Block);
	SIM_EXPECT(Same(back, stored));
}


// The endpoints NAK while Process() writes the medium. The data of the chunk is taken, the medium is written only in Process(), and the
// OUT endpoint takes no more data until then.
void OutNaksWhileProcessWrites() {
	Session<Block, Disk> session;

	const std::array<uint8, 10> write = Cdb10(0x2A, 2, 1);
	std::array<uint8, MscCbwLength> cbw = BotHost::MakeCbw(78, Block, false, write);
	SIM_EXPECT(session.fixture.sim.HostOut(OutAddress, cbw).IsOk());

	std::array<uint8, Block> chunk = MakePattern<Block>(13);
	SIM_EXPECT(session.fixture.sim.HostOut(OutAddress, chunk).IsOk());
	SIM_EXPECT(!session.fixture.sim.HasArmedTransfer(OutAddress));
	SIM_EXPECT_EQ(session.fixture.sim.HostOut(OutAddress, chunk).Error(), ResultStatus::notReady);
	SIM_EXPECT(session.fixture.disk.Storage().subspan(2 * Block, Block)[0] == 0);

	session.fixture.device.Process();
	SIM_EXPECT(Same(session.fixture.disk.Storage().subspan(2 * Block, Block), chunk));

	std::array<uint8, MscCswLength> status{};
	uint32 count = 0;
	SIM_EXPECT_EQ(session.fixture.sim.HostIn(InAddress, status, count), ResultStatus::ok);
	SIM_EXPECT_EQ(count, MscCswLength);
	SIM_EXPECT_EQ(status[12], MscStatusPassed);
}


// A write protected medium refuses WRITE(10) before any data moves: the host's data is stalled, the CSW fails with the whole length as
// residue, and the sense is DATA PROTECT, WRITE PROTECTED. READ still works.
void WriteProtectedMediumRefusesWrites() {
	Session<Block, Disk> session;
	session.fixture.disk.SetReadOnly(true);
	session.fixture.device.Process();

	std::array<uint8, 10> write = Cdb10(0x2A, 0, 1);
	Outcome refused = session.Run(write, Block, false, FiveBlocks, {});
	SIM_EXPECT_EQ(refused.csw.status, MscStatusFailed);
	SIM_EXPECT_EQ(refused.csw.residue, Block);
	SIM_EXPECT_EQ(refused.dataOut, 0);
	SIM_EXPECT(refused.outStalled);
	SIM_EXPECT(AllZeroBlocks(session.fixture.disk.Storage(), 1));
	ExpectSense(session.Sense(), 0x07, 0x27, 0x00);

	std::array<uint8, 10> read = Cdb10(0x28, 0, 1);
	std::array<uint8, Block> back{};
	Outcome reread = session.Run(read, Block, true, {}, back);
	SIM_EXPECT_EQ(reread.csw.status, MscStatusPassed);
}


// A read that fails in the middle of a transfer: the blocks before it are sent, the IN pipe is stalled to end the transfer, and the
// CSW fails with the bytes not sent as residue. The sense is MEDIUM ERROR, UNRECOVERED READ ERROR.
void ReadErrorEndsTheTransfer() {
	Session<Block, FaultyDisk> session;
	session.fixture.disk.failRead = 2;

	std::array<uint8, 10> read = Cdb10(0x28, 0, 4);
	std::array<uint8, 4 * Block> back{};
	Outcome outcome = session.Run(read, 4 * Block, true, {}, back);
	SIM_EXPECT_EQ(outcome.dataIn, 2 * Block);
	SIM_EXPECT(outcome.inStalled);
	SIM_EXPECT_EQ(outcome.csw.status, MscStatusFailed);
	SIM_EXPECT_EQ(outcome.csw.residue, 2 * Block);
	ExpectSense(session.Sense(), 0x03, 0x11, 0x00);
}


// A write that fails in the middle of a transfer: the blocks before it are written, the OUT pipe is stalled, and the residue counts
// only what was written. The failed chunk does not count, and the sense is MEDIUM ERROR, WRITE ERROR.
void WriteErrorEndsTheTransfer() {
	Session<Block, FaultyDisk> session;
	session.fixture.disk.failWrite = 1;

	std::array<uint8, 10> write = Cdb10(0x2A, 0, 3);
	Outcome outcome = session.Run(write, 3 * Block, false, FiveBlocks, {});
	SIM_EXPECT_EQ(outcome.csw.status, MscStatusFailed);
	SIM_EXPECT_EQ(outcome.csw.residue, 2 * Block);
	SIM_EXPECT(outcome.outStalled);
	SIM_EXPECT(Same(session.fixture.disk.Storage().first(Block), std::span<const uint8>(FiveBlocks).first(Block)));
	SIM_EXPECT(AllZeroBlocks(session.fixture.disk.Storage().subspan(Block), 1));
	ExpectSense(session.Sense(), 0x03, 0x0C, 0x00);
}


// A verify that meets a bad block fails the command, with the same sense as a read.
void VerifyErrorFailsTheCommand() {
	Session<Block, FaultyDisk> session;
	session.fixture.disk.failRead = 1;

	std::array<uint8, 10> verify = Cdb10(0x2F, 0, 3);
	Outcome outcome = session.Run(verify, 0, false, {}, {});
	SIM_EXPECT_EQ(outcome.csw.status, MscStatusFailed);
	ExpectSense(session.Sense(), 0x03, 0x11, 0x00);
}


// A reset while the device has armed the OUT endpoint for a write: the armed transfer is abandoned by a stall, the reset recovery clears
// it, and the next command runs. The medium is not touched.
void ResetWhileWriteIsArmed() {
	Session<Block, Disk> session;

	const std::array<uint8, 10> write = Cdb10(0x2A, 0, 2);
	std::array<uint8, MscCbwLength> cbw = BotHost::MakeCbw(79, 2 * Block, false, write);
	SIM_EXPECT(session.fixture.sim.HostOut(OutAddress, cbw).IsOk());
	SIM_EXPECT(session.fixture.sim.HasArmedTransfer(OutAddress));

	SIM_EXPECT_EQ(session.host.Reset(), ResultStatus::ok);
	SIM_EXPECT(!session.fixture.sim.HasArmedTransfer(OutAddress));
	SIM_EXPECT(session.fixture.sim.IsStalled(OutAddress));
	SIM_EXPECT_EQ(session.host.ClearHalt(OutAddress), ResultStatus::ok);
	SIM_EXPECT_EQ(session.host.ClearHalt(InAddress), ResultStatus::ok);

	const std::array<uint8, 6> tur{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	Outcome outcome = session.Run(tur, 0, false, {}, {});
	SIM_EXPECT_EQ(outcome.csw.status, MscStatusPassed);
	SIM_EXPECT(AllZeroBlocks(session.fixture.disk.Storage(), 2));
}


}


int main() {
	RoundTripAcrossBlockChunks();
	RoundTripAcrossLargeChunks();
	ReadShorterThanTheDeviceHas();
	ReadLongerThanTheDeviceHas();
	InNaksWhileProcessReads();
	OutNaksWhileProcessWrites();
	WriteProtectedMediumRefusesWrites();
	ReadErrorEndsTheTransfer();
	WriteErrorEndsTheTransfer();
	VerifyErrorFailsTheCommand();
	ResetWhileWriteIsArmed();
	return SimTest::Finish("TestMscMedia");
}
