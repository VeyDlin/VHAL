#include "SimCheck.h"
#include "ReliableChains.h"
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <optional>
#include <span>

// A peer that reboots in the middle of a run. A reboot is a stack built again on the same line end: it has no state, and its numbers
// start again. Nothing sent after the reboot may be dropped as a duplicate, and the restart is reported to the application.


// One round of a reporting packet line, as in the delivery tests.
template<typename Stack>
void Round(ReportingPacketLine& line, Stack& near, Stack& far) {
	near.Process();
	line.DeliverToB(far);
	far.Process();
	line.DeliverToA(near);
	near.Process();
}


// Sends the payload of id as soon as the layer takes it, running rounds between the tries. Returns true when it was taken.
template<typename Stack>
bool SendWhenReady(ReportingPacketLine& line, Stack& near, Stack& far, uint16 id, std::size_t maxRounds) {
	for (std::size_t round = 0; round < maxRounds; round++) {
		if (ReliableSend(near, id) == ResultStatus::ok) {
			return true;
		}

		Round(line, near, far);
	}

	return false;
}


// The far stack is rebuilt in the middle of a run. The packet sent right after the reboot is refused (the far has no state, so it asks
// for a session) and its outcome is reset; the packets after the session starts are delivered once each, and the restart is reported
// once, on the sender.
void TestFarRebootIsReportedAndDeliveryResumes() {
	ResetReliableState();
	ReportingPacketLine line(VUB::Faults{});
	std::optional<ReliablePacket> farStack;
	farStack.emplace(line.b);
	ReliablePacket near(line.a);
	GiveSession(near);
	near.Process();
	farStack->Process();
	for (uint16 id = 0; id < 3; id++) {
		SIM_EXPECT(SendWhenReady(line, near, *farStack, id, 4));
		for (std::size_t round = 0; round < 3; round++) {
			Round(line, near, *farStack);
		}
	}

	SIM_EXPECT(testRestarts == 0);
	farStack.reset();
	farStack.emplace(line.b);
	farStack->Process();

	SIM_EXPECT(ReliableSend(near, 3) == ResultStatus::ok);
	for (std::size_t round = 0; round < 6; round++) {
		Round(line, near, *farStack);
	}

	SIM_EXPECT(SendWhenReady(line, near, *farStack, 4, 6));
	for (std::size_t round = 0; round < 6; round++) {
		Round(line, near, *farStack);
	}

	for (uint16 id = 0; id < 3; id++) {
		SIM_EXPECT(testDelivered[id] == 1);
		SIM_EXPECT(testStatus[id] == ResultStatus::ok);
	}

	SIM_EXPECT(testDelivered[3] == 0);
	SIM_EXPECT(testOutcomes[3] == 1);
	SIM_EXPECT(testStatus[3] == ResultStatus::reset);
	SIM_EXPECT(testDelivered[4] == 1);
	SIM_EXPECT(testOutcomes[4] == 1);
	SIM_EXPECT(testStatus[4] == ResultStatus::ok);
	SIM_EXPECT(testRestarts == 1);
}


// The near stack is rebuilt in the middle of a run, and the value it is given after the reboot (0x0205, where the first boot had 0x0100)
// gives its session a different tag (0x05, from the starting number 0x0205). The far stack has state for the old session: it takes the sync as a restart, reports it, and delivers the
// packets of the session after the reboot.
void TestNearRebootIsReportedOnTheReceiver() {
	ResetReliableState();
	testNowUs = 0x0100;
	ReportingPacketLine line(VUB::Faults{});
	ReliablePacket far(line.b);
	std::optional<ReliablePacket> nearStack;
	nearStack.emplace(line.a);
	GiveSession(*nearStack, 0x0100);
	nearStack->Process();
	far.Process();
	for (uint16 id = 0; id < 3; id++) {
		SIM_EXPECT(SendWhenReady(line, *nearStack, far, id, 4));
		for (std::size_t round = 0; round < 3; round++) {
			Round(line, *nearStack, far);
		}
	}

	SIM_EXPECT(testRestarts == 0);
	nearStack.reset();
	testNowUs = 0x0205;
	line.SetNow(0x0205);
	nearStack.emplace(line.a);
	GiveSession(*nearStack, 0x0205);
	nearStack->Process();
	SIM_EXPECT(SendWhenReady(line, *nearStack, far, 3, 4));
	for (std::size_t round = 0; round < 6; round++) {
		Round(line, *nearStack, far);
	}

	SIM_EXPECT(testDelivered[3] == 1);
	SIM_EXPECT(testStatus[3] == ResultStatus::ok);
	SIM_EXPECT(testRestarts == 1);
}


// The sync packet is delivered and its acknowledgement is lost. The sender resends it with the same starting number: the receiver knows
// that session, so the copy is acknowledged, and no restart is reported.
void TestResentSyncIsADuplicateNotARestart() {
	ResetReliableState();
	testNowUs = 0x0100;
	ReportingPacketLine line(VUB::Faults{});
	ReliablePacket near(line.a);
	GiveSession(near);
	ReliablePacket far(line.b);
	near.Process();
	far.Process();
	SIM_EXPECT(ReliableSend(near, 0) == ResultStatus::busy);
	near.Process();
	line.DeliverToB(far);
	far.Process();
	RecordSink lostAck;
	line.DeliverToA(lostAck);
	for (uint32 now = 0x0101; now <= 400000; now += 1000) {
		testNowUs = now;
		line.SetNow(now);
		near.Process();
		line.DeliverToB(far);
		far.Process();
		line.DeliverToA(near);
		near.Process();
	}

	SIM_EXPECT(testRestarts == 0);
	SIM_EXPECT(SendWhenReady(line, near, far, 0, 4));
	for (std::size_t round = 0; round < 3; round++) {
		Round(line, near, far);
	}

	SIM_EXPECT(testDelivered[0] == 1);
	SIM_EXPECT(testStatus[0] == ResultStatus::ok);
	SIM_EXPECT(testRestarts == 0);
}


// A receiver reboots after it delivered a packet and before its acknowledgement got out. The sender resends the packet to the rebuilt
// receiver, which has no state: the packet is refused and a resync request comes back, so the sender reports the packet reset. The
// packet was delivered once, by the old receiver, and never ok on the sender.
void TestRebootAfterDeliveryBeforeAckIsResetNotDelivered() {
	ResetReliableState();
	testNowUs = 0x0100;
	ReportingPacketLine line(VUB::Faults{});
	std::optional<ReliablePacket> farStack;
	farStack.emplace(line.b);
	ReliablePacket near(line.a);
	GiveSession(near);
	near.Process();
	farStack->Process();
	SIM_EXPECT(SendWhenReady(line, near, *farStack, 0, 4));
	for (std::size_t round = 0; round < 3; round++) {
		Round(line, near, *farStack);
	}

	SIM_EXPECT(ReliableSend(near, 1) == ResultStatus::ok);
	near.Process();
	line.DeliverToB(*farStack);
	farStack->Process();
	RecordSink lostAck;
	line.DeliverToA(lostAck);
	SIM_EXPECT(testDelivered[1] == 1);

	farStack.reset();
	farStack.emplace(line.b);
	farStack->Process();
	for (uint32 now = 0x0101; now <= 1000000; now += 1000) {
		testNowUs = now;
		line.SetNow(now);
		Round(line, near, *farStack);
	}

	SIM_EXPECT(testDelivered[1] == 1);
	SIM_EXPECT(testOutcomes[1] == 1);
	SIM_EXPECT(testStatus[1] == ResultStatus::reset);
	SIM_EXPECT(testRestarts == 1);
}


int main() {
	TestFarRebootIsReportedAndDeliveryResumes();
	TestNearRebootIsReportedOnTheReceiver();
	TestResentSyncIsADuplicateNotARestart();
	TestRebootAfterDeliveryBeforeAckIsResetNotDelivered();
	return SimFinish("TestVubReliableRestart");
}
