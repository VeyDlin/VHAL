#include "SimCheck.h"
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Utilities/Checksum/CRC/Engine/CrcTable.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Modbus.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <span>


// Every code snippet of the VUB::Crc wiki page compiles here. The snippet is copied from .wiki/Common/Drivers/Interface/VUB/Transform/Crc.md.
namespace WikiCrc {
// A packet link that sends whole packets: it takes each packet, or reports busy.
struct BoardLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex();

	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}
};

void OnPacket(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
}

// The packets of the board carry a CRC-16/MODBUS, high byte first. The default engine is the bitwise one.
using Board = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<VUB::Crc<Checksum::Crc16Modbus, VUB::CrcOrder::highByteFirst>>()
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>();
}>;

// The same CRC with the table engine, for a product that has flash to spare for the table.
using Fast = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<VUB::Crc<Checksum::Crc16Modbus::With<Checksum::CrcTable>, VUB::CrcOrder::highByteFirst>>()
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>();
}>;

BoardLink boardLink;
Board board(boardLink);

void BoardSend() {
	std::array<uint8, 3> hello = {1, 2, 3};
	board.Send(std::span<const uint8>(hello.data(), hello.size()));
	board.Process();
}
}  // namespace WikiCrc


// The snippet's chains run: a send of three bytes is accepted and processed.
void TestWikiSnippetRuns() {
	WikiCrc::BoardSend();
	SIM_EXPECT(WikiCrc::Board::count == 2);
	SIM_EXPECT(WikiCrc::Fast::count == 2);
}


// The table engine is not part of the fingerprint: the chain of the snippet and the same chain on the table give one fingerprint.
void TestWikiEnginesShareTheFingerprint() {
	SIM_EXPECT((VUB::Crc<Checksum::Crc16Modbus, VUB::CrcOrder::highByteFirst>::fingerprint ==
		VUB::Crc<Checksum::Crc16Modbus::With<Checksum::CrcTable>, VUB::CrcOrder::highByteFirst>::fingerprint));
}


int main() {
	TestWikiSnippetRuns();
	TestWikiEnginesShareTheFingerprint();
	return SimFinish("TestVubCrcWiki");
}
