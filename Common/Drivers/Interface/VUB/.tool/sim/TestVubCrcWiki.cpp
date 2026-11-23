#include "SimCheck.h"
#include <Drivers/Interface/VUB/Engine/CrcBitwise.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/Blocks.h>
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
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

// The packets of the board carry a CRC-16/MODBUS, high byte first. The table engine is the default.
using Board = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<VUB::Crc16ModbusHighByteFirst>()
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>();
}>;

// The same CRC with the bitwise engine, for a product with no flash to spare for the table.
using Small = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<VUB::Crc<VUB::Crc16Modbus::parameters, VUB::CrcBitwise>>()
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>();
}>;

// CRC-16/AUG-CCITT: polynomial 1021, initial 1D0F, not reflected, final XOR 0000, check E5CC.
inline constexpr VUB::CrcParameters augCcitt = VUB::CrcParameters()
	.Width(16)
	.Polynomial(0x1021)
	.Initial(0x1D0F)
	.FinalXor(0x0000)
	.Check(0xE5CC);

using AugCcitt = VUB::Crc<augCcitt>;

BoardLink boardLink;
Board board(boardLink);

void BoardSend() {
	std::array<uint8, 3> hello = {1, 2, 3};
	board.Send(std::span<const uint8>(hello.data(), hello.size()));
	board.Process();
}
}  // namespace WikiCrc


// The snippet's chain runs: a send of three bytes is accepted and processed.
void TestWikiSnippetRuns() {
	WikiCrc::BoardSend();
	SIM_EXPECT(WikiCrc::Small::count == 2);
}


// The "Other parameters" snippet of the page: its check is run at compile time, and the chain without Check is the same CRC.
void TestWikiOtherParametersSnippet() {
	SIM_EXPECT(WikiCrc::AugCcitt::checked);
	SIM_EXPECT((WikiCrc::AugCcitt::fingerprint == VUB::Crc<VUB::CrcParameters().Width(16).Polynomial(0x1021).Initial(0x1D0F).FinalXor(0x0000)>::fingerprint));
}


int main() {
	TestWikiSnippetRuns();
	TestWikiOtherParametersSnippet();
	return SimFinish("TestVubCrcWiki");
}
