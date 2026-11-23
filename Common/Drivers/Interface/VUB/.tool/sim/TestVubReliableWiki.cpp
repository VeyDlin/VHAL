#include "SimCheck.h"
#include <Drivers/Interface/VUB/Layer/VubReliable.h>
#include <Drivers/Interface/VUB/Vub.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>

// The snippet of the wiki page Layer/VubReliable, as it is written there: it must compile, and its code runs once.

// The packet link of the board: it sends a whole packet, or reports busy when its transmitter has no room.
struct BoardLink {
	static constexpr VubLinkTraits traits = VubLinkTraits().Framed().MaxPacket(64).FullDuplex();

	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}
};

// The clock of the board, in microseconds.
uint32 BoardClockUs() {
	return 0;
}

// Called once for each packet sent with delivery. The payload is valid only during the call.
void OnBoardOutcome(const VubOutcome& outcome) {
	(void)outcome;
}

void OnBoardRestart() {
}

void OnBoardPacket(const VubMeta&, std::span<const uint8> payload) {
	(void)payload;
}

using Board = Vub<+[] {
	return VubBuilder()
		.Layer<VubReliable>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(32)
		.OnOutcome<&OnBoardOutcome>()
		.OnRestart<&OnBoardRestart>()
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnBoardPacket>()
		.Clock<&BoardClockUs>();
}>;

BoardLink boardLink;
Board board(boardLink);

void BoardSend() {
	std::array<uint8, 3> hello = {1, 2, 3};
	board.Send(std::span<const uint8>(hello.data(), hello.size()));
	board.Process();
}

// The application gives the board's layer its session value once per boot, before the first send. The layer is reached with Find<Block>().
// The value differs for each boot: a random number, or the chip's identifier mixed with a boot counter.
void BoardStartSession(uint16 bootValue) {
	board.Find<VubReliable>().SetSession(bootValue);
}


// The same chain, with a link that keeps the first packet it sends, so the test can read the bytes that the session value gave.
struct ProbeLink {
	static constexpr VubLinkTraits traits = VubLinkTraits().Framed().MaxPacket(64).FullDuplex();

	std::array<uint8, 16> first{};
	std::size_t firstLength = 0;
	bool sentFirst = false;

	ResultStatus Send(std::span<const uint8> packet) {
		if (!sentFirst) {
			sentFirst = true;
			firstLength = packet.size();
			for (std::size_t index = 0; index < packet.size() && index < first.size(); index++) {
				first[index] = packet[index];
			}
		}

		return ResultStatus::ok;
	}
};

using Probe = Vub<+[] {
	return VubBuilder()
		.Layer<VubReliable>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(32)
		.OnOutcome<&OnBoardOutcome>()
		.OnRestart<&OnBoardRestart>()
		.Link<ProbeLink>()
		.MaxPayload(32)
		.OnReceive<&OnBoardPacket>()
		.Clock<&BoardClockUs>();
}>;


int main() {
	BoardStartSession(0x1234);
	BoardSend();
	ProbeLink probeLink;
	Probe probe(probeLink);
	probe.Find<VubReliable>().SetSession(0x5A5A);
	std::array<uint8, 3> hello = {1, 2, 3};
	SIM_EXPECT(probe.Send(std::span<const uint8>(hello.data(), hello.size())) == ResultStatus::busy);
	probe.Process();
	// The sync: flags 0x05, tag 0x5A (counter 0 plus the boot value's low byte), number 0x0000, delta 0x00, boot 0x5A5A, counter 0x00.
	std::array<uint8, 8> sync = {0x05, 0x5A, 0x00, 0x00, 0x00, 0x5A, 0x5A, 0x00};
	SIM_EXPECT(probeLink.sentFirst);
	SIM_EXPECT(probeLink.firstLength == sync.size());
	for (std::size_t index = 0; index < sync.size(); index++) {
		SIM_EXPECT(probeLink.first[index] == sync[index]);
	}

	return SimFinish("TestVubReliableWiki");
}
