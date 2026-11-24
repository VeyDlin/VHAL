#include "SimCheck.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/TestKit/Contracts.h>
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Utilities/DataTypes.h>
#include <optional>
#include <span>

// The snippets of the test kit's wiki page. Each one is the text of its page, and each compiles and runs here.


namespace LoopbackSnippet {
void OnPacket(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
}


using Chain = VUB::Stack<+[] {
	return VUB::Builder()
		.Link<VUB::PacketEnd<64, 512>>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>();
}>;


void Run() {
	VUB::Faults faults;
	faults.dropPercent = 5;
	faults.corruptPercent = 5;
	faults.duplicatePercent = 5;
	faults.maxDelayUs = 50;
	faults.seed = 7;

	VUB::PacketLine<64, 512> line(faults);
	Chain sender(line.a);
	Chain receiver(line.b);

	// Each step sets the time, the stacks process, and the line delivers what was sent to the receiving end.
	uint32 now = 0;
	for (int step = 0; step < 10; step++) {
		now += 1000;
		line.SetNow(now);
		sender.Process();
		line.DeliverToB(receiver);
		receiver.Process();
	}
}
}


namespace ChecksSnippet {
bool RunChecks() {
	VUB::Crc8Smbus transform;
	bool transformOk = VUB::CheckTransform(transform, 7, 500, 40);

	VUB::Cobs framing;
	bool framingOk = VUB::CheckResync(framing, 3, 200, 32, 10);

	return transformOk && framingOk;
}
}


namespace OutageSnippet {
void Run() {
	// A line that reports its state, and a schedule of outages from the seed.
	VUB::Faults faults;
	faults.seed = 7;
	faults.outageEveryUs = 500;
	faults.outageForUs = 100;
	VUB::PacketLine<64, 512, true> scheduled(faults);
	scheduled.SetNow(0);
}
}


namespace RebootSnippet {
void OnPacket(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
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


void Run() {
	VUB::PacketLine<64, 512, true> line(VUB::Faults{});
	Near near(line.a);
	std::optional<Near> far;
	far.emplace(line.b);
	far.reset();                 // the far stack is destroyed; its end gives the line back its sink
	line.SetLinkUp(false);       // reported to no one
	near.Process();              // near.LinkUp() is false
	line.SetLinkUp(true);
	far.emplace(line.b);         // the rebooted far stack
	near.Process();              // near.LinkUp() is true again
	far->Process();
}
}


int main() {
	LoopbackSnippet::Run();
	SIM_EXPECT(ChecksSnippet::RunChecks());
	OutageSnippet::Run();
	RebootSnippet::Run();
	return SimFinish("TestVubTestKitWiki");
}
