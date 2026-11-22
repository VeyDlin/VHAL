#include "SimCheck.h"
#include <Drivers/Interface/Modbus/RTU/RtuFramer.h>
#include <Utilities/Checksum/CRC/Crc.h>
#include <array>
#include <cstddef>
#include <span>


// The CRC of 01 03 00 00 00 0A is C5 CD on the wire, so the value is 0xCDC5 (the low byte is sent first). Build-time evaluation:
consteval uint16 CrcOfExample() {
	std::array<uint8, 6> data = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
	return RtuFramer::Crc(data);
}


static_assert(CrcOfExample() == 0xCDC5);


void TestCrcExample() {
	std::array<uint8, 6> data = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
	SIM_EXPECT(RtuFramer::Crc(data) == 0xCDC5);
}


// The framer table must give the same CRC as the library's table-free definition on random frames. The generator is a fixed
// linear congruential sequence, so every run checks the same 1000 frames.
void TestCrcAgainstLibrary() {
	uint32 seed = 12345;
	std::array<uint8, 256> data{};
	bool allMatch = true;

	for (int frameIndex = 0; frameIndex < 1000; frameIndex++) {
		seed = seed * 1664525u + 1013904223u;
		std::size_t length = 1 + (seed >> 24) % 255;

		for (std::size_t index = 0; index < length; index++) {
			seed = seed * 1664525u + 1013904223u;
			data[index] = static_cast<uint8>(seed >> 24);
		}

		uint16 expected = Crc::Calculate(data.data(), length, Crc::CRC_16_MODBUS());
		uint16 actual = RtuFramer::Crc(std::span<const uint8>(data.data(), length));
		if (expected != actual) {
			allMatch = false;
		}
	}

	SIM_EXPECT(allMatch);
}


void TestBuild() {
	// The PDU 03 00 00 00 0A in a frame for slave 1 is the example frame: 01 03 00 00 00 0A C5 CD.
	std::array<uint8, 5> pdu = {0x03, 0x00, 0x00, 0x00, 0x0A};
	std::array<uint8, 8> frame{};
	SIM_EXPECT(RtuFramer::Build(frame, 0x01, pdu) == ResultStatus::ok);
	const std::array<uint8, 8> expected = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD};
	SIM_EXPECT(frame == expected);
	SIM_EXPECT(RtuFramer::FrameSize(pdu.size()) == 8);

	std::array<uint8, 7> small{};
	SIM_EXPECT(RtuFramer::Build(small, 0x01, pdu) == ResultStatus::invalidParameter);

	std::array<uint8, 0> none{};
	SIM_EXPECT(RtuFramer::Build(frame, 0x01, none) == ResultStatus::invalidParameter);

	std::array<uint8, 254> tooLong{};
	std::array<uint8, 300> roomy{};
	SIM_EXPECT(RtuFramer::Build(roomy, 0x01, tooLong) == ResultStatus::invalidParameter);

	std::array<uint8, 253> longest{};
	SIM_EXPECT(RtuFramer::Build(roomy, 0x01, longest) == ResultStatus::ok);
	SIM_EXPECT(RtuFramer::FrameSize(253) == RtuFramer::MaxFrameSize);
}


void TestCheck() {
	std::array<uint8, 8> good = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD};
	SIM_EXPECT(RtuFramer::Check(good, 0x01) == ResultStatus::ok);

	// One changed data byte: the CRC fails first.
	std::array<uint8, 8> flipped = good;
	flipped[4] = 0x01;
	SIM_EXPECT(RtuFramer::Check(flipped, 0x01) == ResultStatus::crcError);

	// A changed CRC byte also fails.
	std::array<uint8, 8> badCrc = good;
	badCrc[7] = 0xCC;
	SIM_EXPECT(RtuFramer::Check(badCrc, 0x01) == ResultStatus::crcError);

	// A valid frame for another slave is invalidData (the CRC is right, the address is not).
	SIM_EXPECT(RtuFramer::Check(good, 0x02) == ResultStatus::invalidData);

	// A broadcast frame is accepted by a caller that expects address 0. The CRC comes from the library, not from the framer.
	std::array<uint8, 6> body = {0x00, 0x03, 0x00, 0x00, 0x00, 0x0A};
	uint16 bodyCrc = Crc::Calculate(body.data(), body.size(), Crc::CRC_16_MODBUS());
	std::array<uint8, 8> broadcast = {0x00, 0x03, 0x00, 0x00, 0x00, 0x0A, static_cast<uint8>(bodyCrc & 0xFF), static_cast<uint8>(bodyCrc >> 8)};
	SIM_EXPECT(RtuFramer::Check(broadcast, RtuFramer::BroadcastAddress) == ResultStatus::ok);
	SIM_EXPECT(RtuFramer::Check(broadcast, 0x01) == ResultStatus::invalidData);

	// A frame shorter than four bytes cannot hold a CRC and an address.
	std::array<uint8, 3> tiny = {0x01, 0x03, 0x00};
	SIM_EXPECT(RtuFramer::Check(tiny, 0x01) == ResultStatus::invalidData);
}


void TestPdu() {
	std::array<uint8, 8> good = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD};
	std::span<const uint8> pdu = RtuFramer::Pdu(good);
	SIM_EXPECT(pdu.size() == 5);
	SIM_EXPECT(pdu[0] == 0x03 && pdu[4] == 0x0A);

	std::array<uint8, 3> tiny = {0x01, 0x03, 0x00};
	SIM_EXPECT(RtuFramer::Pdu(tiny).empty());
}


int main() {
	TestCrcExample();
	TestCrcAgainstLibrary();
	TestBuild();
	TestCheck();
	TestPdu();
	return SimFinish("TestRtuFramer");
}
