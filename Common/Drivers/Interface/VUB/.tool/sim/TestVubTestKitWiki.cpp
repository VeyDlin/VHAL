#include "SimCheck.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/TestKit/Contracts.h>
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Utilities/Checksum/CRC/Preset/Crc8Smbus.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <bit>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

// The snippets of the test kit's wiki page. Each one is the text of its page, and each compiles and runs here. The loopback and reboot
// snippets keep the payloads their handler gets in a list, and their Run() returns what the test checks, since a snippet's locals are not
// visible outside it.


namespace LoopbackSnippet {
std::vector<std::vector<uint8>> received;


void OnPacket(const VUB::Meta&, std::span<const uint8> payload) {
	received.emplace_back(payload.begin(), payload.end());
}


using Chain = VUB::Stack<+[] {
	return VUB::Builder()
		.Link<VUB::PacketEnd<64, 512>>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>();
}>;


// Returns the counts of the wire that carries the packets from the sender to the receiver.
VUB::WireCounts Run() {
	VUB::Faults faults;
	faults.dropPercent = 5;
	faults.corruptPercent = 5;
	faults.duplicatePercent = 5;
	faults.maxDelayUs = 50;
	faults.seed = 7;

	VUB::PacketLine<64, 512> line(faults);
	Chain sender(line.a);
	Chain receiver(line.b);

	// Each step sets the time, the sender sends a packet, the stacks process, and the line delivers what was sent to the receiving end.
	uint32 now = 0;
	for (uint8 step = 0; step < 10; step++) {
		now += 1000;
		line.SetNow(now);
		std::array<uint8, 1> payload = { step };
		sender.Send(payload);
		sender.Process();
		line.DeliverToB(receiver);
		receiver.Process();
	}

	// The packets of the last step are delayed by up to maxDelayUs. The line delivers them at the next time, and the receiver takes them.
	now += 1000;
	line.SetNow(now);
	line.DeliverToB(receiver);
	receiver.Process();
	return line.toB.Counts();
}
}


namespace ChecksSnippet {
bool RunChecks() {
	VUB::Crc<Checksum::Crc8Smbus> transform;
	bool transformOk = VUB::CheckTransform(transform, 7, 500, 40);

	VUB::Cobs framing;
	bool framingOk = VUB::CheckResync(framing, 3, 200, 32, 10);

	return transformOk && framingOk;
}
}


namespace OutageSnippet {
// Returns the counts of the wire that carries the packets from end a: a packet every 10 us for 2 ms, on a line with a schedule of outages.
VUB::WireCounts Run() {
	// A line that reports its state, and a schedule of outages from the seed.
	VUB::Faults faults;
	faults.seed = 7;
	faults.outageEveryUs = 500;
	faults.outageForUs = 100;
	VUB::PacketLine<64, 512, true> scheduled(faults);
	scheduled.SetNow(0);
	std::array<uint8, 1> payload = { 0 };
	for (uint32 now = 10; now <= 2000; now += 10) {
		scheduled.SetNow(now);
		scheduled.a.Send(payload);
	}

	return scheduled.toB.Counts();
}
}


namespace RebootSnippet {
std::vector<std::vector<uint8>> received;


void OnPacket(const VUB::Meta&, std::span<const uint8> payload) {
	received.emplace_back(payload.begin(), payload.end());
}


uint32 Now() {
	return 0;
}


// A peer that reboots: the far stack is destroyed and built again, and the near stack keeps working.
using Near = VUB::Stack<+[] {
	return VUB::Builder()
		.Link<VUB::PacketEnd<64, 512, true>>()
		.MaxPayload(32)
		.WhileDown(VUB::Down::Fail)
		.OnReceive<&OnPacket>()
		.Clock<&Now>();
}>;


// Returns the near stack's link state during the outage, and after the reboot.
std::array<bool, 2> Run() {
	VUB::PacketLine<64, 512, true> line(VUB::Faults{});
	Near near(line.a);
	std::optional<Near> far;
	far.emplace(line.b);
	far.reset();                 // the far stack is destroyed; its end gives the line back its sink
	line.SetLinkUp(false);       // reported to no one
	near.Process();              // near.LinkUp() is false
	bool linkDuringOutage = near.LinkUp();
	line.SetLinkUp(true);
	far.emplace(line.b);         // the rebooted far stack
	near.Process();              // near.LinkUp() is true again
	std::array<uint8, 1> payload = { 1 };
	near.Send(payload);          // the near stack keeps working: the packet reaches the rebooted far stack
	near.Process();
	line.DeliverToB(*far);
	far->Process();
	return { linkDuringOutage, near.LinkUp() };
}
}


// Whether a received byte is one the loopback sent (0 to 9), or one of them with one bit flipped: the corruption of the wire.
bool NearSentValue(uint8 value) {
	for (uint8 step = 0; step < 10; step++) {
		if (std::popcount(static_cast<unsigned int>(value ^ step)) <= 1) {
			return true;
		}
	}

	return false;
}


void CheckLoopback() {
	LoopbackSnippet::received.clear();
	VUB::WireCounts counts = LoopbackSnippet::Run();
	std::size_t arrived = LoopbackSnippet::received.size();

	// Ten packets were pushed, and none was lost to an outage or to a full wire.
	SIM_EXPECT(counts.pushed == 10);
	SIM_EXPECT(counts.outaged == 0);
	SIM_EXPECT(counts.overflowed == 0);

	// Each packet is dropped or stored, and a duplicate is a second stored copy. Every stored copy reaches the receiver.
	SIM_EXPECT(counts.delivered == counts.pushed - counts.dropped + counts.duplicated);
	SIM_EXPECT(arrived == counts.delivered);
	SIM_EXPECT(arrived > 0);

	// Each payload is one byte, and it is a sent value or a sent value with one bit flipped.
	for (const std::vector<uint8>& payload : LoopbackSnippet::received) {
		SIM_EXPECT(payload.size() == 1 && NearSentValue(payload[0]));
	}

	// The same seed gives the same faults and the same arrivals.
	LoopbackSnippet::received.clear();
	VUB::WireCounts again = LoopbackSnippet::Run();
	SIM_EXPECT(again.dropped == counts.dropped);
	SIM_EXPECT(again.corrupted == counts.corrupted);
	SIM_EXPECT(again.duplicated == counts.duplicated);
	SIM_EXPECT(LoopbackSnippet::received.size() == arrived);
}


void CheckOutage() {
	VUB::WireCounts counts = OutageSnippet::Run();

	// A packet every 10 us from 10 us to 2 ms: 200 packets, none lost to the wire's faults or its slots.
	SIM_EXPECT(counts.pushed == 200);
	SIM_EXPECT(counts.dropped == 0);
	SIM_EXPECT(counts.overflowed == 0);

	// The schedule takes the line down for 100 us at a time, so some packets are lost as outaged, and the line is up for the others.
	SIM_EXPECT(counts.outaged >= 5);
	SIM_EXPECT(counts.outaged < counts.pushed);

	// The schedule comes from the seed: the same run takes the same outages.
	VUB::WireCounts again = OutageSnippet::Run();
	SIM_EXPECT(again.outaged == counts.outaged);
}


void CheckReboot() {
	RebootSnippet::received.clear();
	std::array<bool, 2> states = RebootSnippet::Run();

	// The near stack learns of the outage and of the reboot: the link is down during the outage and up after the reboot.
	SIM_EXPECT(!states[0]);
	SIM_EXPECT(states[1]);

	// The packet that the near stack sends after the reboot reaches the rebooted far stack, and it is the only one.
	SIM_EXPECT(RebootSnippet::received.size() == 1);
	SIM_EXPECT(RebootSnippet::received.size() == 1 && RebootSnippet::received[0].size() == 1 && RebootSnippet::received[0][0] == 1);
}


int main() {
	CheckLoopback();
	SIM_EXPECT(ChecksSnippet::RunChecks());
	CheckOutage();
	CheckReboot();
	return SimFinish("TestVubTestKitWiki");
}
