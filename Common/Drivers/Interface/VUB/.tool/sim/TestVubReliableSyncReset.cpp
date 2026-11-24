#include "ReliableChains.h"
#include "SimCheck.h"
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <optional>

// A peer that reboots while packets of the old session are in flight toward it. The sending side resets its packets at once when the
// rebooted sync arrives, if it holds a session of that peer (it has received from the peer). If it holds none, the sync opens the session
// and the reset comes later, at the resync that the rebooted peer asks for when the old packet reaches it. The packets after the resync
// are delivered in both cases.


// One round of a line, with the stack on each end. a is on line.a and b on line.b.
template<typename Stack>
void Carry(ReportingPacketLine& line, Stack& a, Stack& b) {
	a.Process();
	line.DeliverToB(b);
	b.Process();
	line.DeliverToA(a);
	a.Process();
}


// Sends the payload of id from a (fromA) or from b, running rounds between the tries. Returns true when the layer took it.
template<typename Stack>
bool SendWhenReady(ReportingPacketLine& line, Stack& a, Stack& b, bool fromA, uint16 id, std::size_t maxRounds) {
	for (std::size_t round = 0; round < maxRounds; round++) {
		if (ReliableSend(fromA ? a : b, id) == ResultStatus::ok) {
			return true;
		}

		Carry(line, a, b);
	}

	return false;
}


// The peer B has received from A's side and then reboots while A holds a session of B. The sync of the rebooted B reaches A, which
// resets its packet in flight at once, at the sync: the restart runs and the packet's outcome is reset before any further packet is sent.
void TestSyncOfRebootResetsAtOnceWhenSessionHeld() {
	ResetReliableState();
	testNowUs = 0x0100;
	ReportingPacketLine line(VUB::Faults{});
	ReliablePacket a(line.a);
	std::optional<ReliablePacket> b;
	b.emplace(line.b);
	GiveSession(a, 0x0100);
	GiveSession(*b, 0x2200);
	a.Process();
	b->Process();

	// B sends id 10 to A, so A receives from B and holds a session of it.
	SIM_EXPECT(SendWhenReady(line, a, *b, false, 10, 4));
	for (std::size_t round = 0; round < 6; round++) {
		Carry(line, a, *b);
	}

	SIM_EXPECT(testDelivered[10] == 1);
	SIM_EXPECT(testStatus[10] == ResultStatus::ok);

	// A starts its session with B (its sync is acknowledged), then sends id 2 and keeps it in the line toward B.
	SIM_EXPECT(SendWhenReady(line, a, *b, true, 1, 4));
	for (std::size_t round = 0; round < 3; round++) {
		Carry(line, a, *b);
	}

	SIM_EXPECT(ReliableSend(a, 2) == ResultStatus::ok);
	a.Process();
	SIM_EXPECT(testOutcomes[2] == 0);
	SIM_EXPECT(testRestarts == 0);

	// B reboots with a new boot value. Its sync is carried to A only, and the line toward B keeps packet 2.
	b.reset();
	b.emplace(line.b);
	GiveSession(*b, 0x3300);
	b->Process();
	SIM_EXPECT(ReliableSend(*b, 11) == ResultStatus::busy);
	b->Process();
	line.DeliverToA(a);
	a.Process();

	SIM_EXPECT(testOutcomes[2] == 1);
	SIM_EXPECT(testStatus[2] == ResultStatus::reset);
	SIM_EXPECT(testRestarts == 1);

	// A sends id 3 after the reset. Packet 2 then reaches the rebooted B, which refuses it, and B's sync is answered. Packet 3 is delivered.
	SIM_EXPECT(SendWhenReady(line, a, *b, true, 3, 8));
	for (std::size_t round = 0; round < 8; round++) {
		Carry(line, a, *b);
	}

	SIM_EXPECT(testDelivered[2] == 0);
	SIM_EXPECT(testOutcomes[2] == 1);
	SIM_EXPECT(testDelivered[3] == 1);
	SIM_EXPECT(testStatus[3] == ResultStatus::ok);
	SIM_EXPECT(testRestarts == 1);
}


// The peer B has never sent to A, so A holds no session of it. The sync of the rebooted B opens the session at A and resets nothing: the
// packets in flight are still waiting. A's next packet, which carries the old session's tag, is refused by the rebooted B, which asks for
// a resync. The resync resets the packets in flight and the packet sent after the reboot, and the packets after the new session are
// delivered.
void TestSyncWithoutSessionResetsAtResync() {
	ResetReliableState();
	testNowUs = 0x0100;
	ReportingPacketLine line(VUB::Faults{});
	ReliablePacket a(line.a);
	std::optional<ReliablePacket> b;
	b.emplace(line.b);
	GiveSession(a, 0x0100);
	GiveSession(*b, 0x2200);
	a.Process();
	b->Process();

	// A starts its session with B and sends id 1, which is delivered. B never sends, so A holds no session of B.
	SIM_EXPECT(SendWhenReady(line, a, *b, true, 1, 4));
	for (std::size_t round = 0; round < 6; round++) {
		Carry(line, a, *b);
	}

	SIM_EXPECT(testDelivered[1] == 1);
	SIM_EXPECT(testStatus[1] == ResultStatus::ok);
	SIM_EXPECT(testRestarts == 0);

	// A sends id 2 and keeps it in the line toward B.
	SIM_EXPECT(ReliableSend(a, 2) == ResultStatus::ok);
	a.Process();

	// B reboots with a new boot value. Its sync is carried to A only.
	b.reset();
	b.emplace(line.b);
	GiveSession(*b, 0x3300);
	b->Process();
	SIM_EXPECT(ReliableSend(*b, 11) == ResultStatus::busy);
	b->Process();
	line.DeliverToA(a);
	a.Process();

	SIM_EXPECT(testOutcomes[2] == 0);
	SIM_EXPECT(testRestarts == 0);

	// A sends id 3 with the old session's tag. Packets 2 and 3 are refused by the rebooted B, which asks for a resync, and A resets them.
	SIM_EXPECT(ReliableSend(a, 3) == ResultStatus::ok);
	for (std::size_t round = 0; round < 8; round++) {
		Carry(line, a, *b);
	}

	SIM_EXPECT(testDelivered[2] == 0);
	SIM_EXPECT(testStatus[2] == ResultStatus::reset);
	SIM_EXPECT(testOutcomes[2] == 1);
	SIM_EXPECT(testDelivered[3] == 0);
	SIM_EXPECT(testStatus[3] == ResultStatus::reset);
	SIM_EXPECT(testOutcomes[3] == 1);
	SIM_EXPECT(testRestarts == 1);

	// Every packet after the resync is delivered once, each with the outcome ok.
	for (uint16 id = 4; id < 7; id++) {
		SIM_EXPECT(SendWhenReady(line, a, *b, true, id, 8));
		for (std::size_t round = 0; round < 6; round++) {
			Carry(line, a, *b);
		}

		SIM_EXPECT(testDelivered[id] == 1);
		SIM_EXPECT(testStatus[id] == ResultStatus::ok);
	}

	SIM_EXPECT(testRestarts == 1);
}


int main() {
	TestSyncOfRebootResetsAtOnceWhenSessionHeld();
	TestSyncWithoutSessionResetsAtResync();
	return SimFinish("TestVubReliableSyncReset");
}
