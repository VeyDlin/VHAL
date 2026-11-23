#include "SimCheck.h"
#include "ReliableChains.h"
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Drivers/Interface/VUB/TestKit/Wire.h>
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <span>


// Packets over the faulty loopback: drops, corrupted bits, duplicates and delays (which reorder). The invariant checked for every
// packet: it is delivered at most once, and its outcome is reported exactly once; an ok outcome means it was delivered, a failed one
// means it was not acknowledged within the retries (it may still have arrived, so it is delivered at most once).


// Counts what the handlers saw and checks the invariant for ids 0 to total - 1. Returns the number of ok outcomes.
std::size_t CheckExactlyOnce(std::size_t total, std::size_t& failed) {
	std::size_t okCount = 0;
	failed = 0;
	for (std::size_t id = 0; id < total; id++) {
		SIM_EXPECT(testOutcomes[id] == 1);
		SIM_EXPECT(testDelivered[id] <= 1);
		if (testStatus[id] == ResultStatus::ok) {
			SIM_EXPECT(testDelivered[id] == 1);
			okCount++;
		} else {
			SIM_EXPECT(testStatus[id] == ResultStatus::retryExhausted);
			failed++;
		}
	}

	for (std::size_t id = total; id < ReliableIds; id++) {
		SIM_EXPECT(testDelivered[id] == 0);
	}

	return okCount;
}


// Sends total packets, one attempt per step, and runs both stacks each step over a line with faults. Stops when every outcome is in or
// after the time limit.
template<typename Line, typename Stack>
void RunFaultyTraffic(Line& line, Stack& near, Stack& far, std::size_t total, uint32 stepUs, uint32 limitUs) {
	std::size_t sent = 0;
	uint32 now = 0;
	while (now < limitUs) {
		now += stepUs;
		testNowUs = now;
		line.SetNow(now);
		if (sent < total && ReliableSend(near, static_cast<uint16>(sent)) == ResultStatus::ok) {
			sent++;
		}

		near.Process();
		line.DeliverToB(far);
		far.Process();
		line.DeliverToA(near);
		near.Process();
		std::size_t settled = 0;
		for (std::size_t id = 0; id < sent; id++) {
			if (testOutcomes[id] == 1) {
				settled++;
			}
		}

		if (sent == total && settled == total) {
			break;
		}
	}
}


// A stream with 1 % dropped bytes and 1 % duplicated bytes. A frame with a lost or repeated byte fails the framing's checks and is
// dropped, and the layer resends it. The stream has no delays: the check doubles cannot see two bytes swapped, so a stream whose bytes
// reorder would hand the layer damage that looks valid. Reordering is tested on the packet line.
void TestStreamWithByteFaults() {
	ResetReliableState();
	VUB::Faults faults;
	faults.seed = 0x2468;
	faults.dropPercent = 1;
	faults.duplicatePercent = 1;
	ReportingStreamLine line(faults);
	ReliableStream near(line.a);
	GiveSession(near);
	ReliableStream far(line.b);
	GiveSession(far);
	near.Process();
	far.Process();
	const std::size_t total = 120;
	RunFaultyTraffic(line, near, far, total, 100, 60000000);
	std::size_t failed = 0;
	std::size_t okCount = CheckExactlyOnce(total, failed);
	SIM_EXPECT(okCount + failed == total);
	SIM_EXPECT(okCount > total / 2);
	SIM_EXPECT(line.toB.Counts().dropped + line.toA.Counts().dropped > 0);
	SIM_EXPECT(line.toB.Counts().duplicated + line.toA.Counts().duplicated > 0);
	SIM_EXPECT(testRestarts == 0);
}


// A packet line with 20 % drops, 10 % corrupted packets, 10 % duplicates, and delays up to 500 us, so packets overtake each other.
void TestPacketsWithFaultsAndReordering() {
	ResetReliableState();
	VUB::Faults faults;
	faults.seed = 0x1357;
	faults.dropPercent = 20;
	faults.corruptPercent = 10;
	faults.duplicatePercent = 10;
	faults.maxDelayUs = 500;
	ReportingPacketLine line(faults);
	ReliablePacket near(line.a);
	GiveSession(near);
	ReliablePacket far(line.b);
	GiveSession(far);
	near.Process();
	far.Process();
	const std::size_t total = 150;
	RunFaultyTraffic(line, near, far, total, 100, 120000000);
	std::size_t failed = 0;
	std::size_t okCount = CheckExactlyOnce(total, failed);
	SIM_EXPECT(okCount + failed == total);
	SIM_EXPECT(okCount > total / 2);
	SIM_EXPECT(line.toB.Counts().duplicated + line.toA.Counts().duplicated > 0);
	SIM_EXPECT(line.toB.Counts().dropped + line.toA.Counts().dropped > 0);
	SIM_EXPECT(testRestarts == 0);
}


// Traffic in both directions at once: each side sends its own ids, and each side's packets are delivered exactly once. The ids of the
// far side start at 256, so the two sets do not share a count.
void TestBothDirectionsWithFaults() {
	ResetReliableState();
	VUB::Faults faults;
	faults.seed = 0x0BAD;
	faults.dropPercent = 10;
	faults.duplicatePercent = 5;
	faults.maxDelayUs = 200;
	ReportingPacketLine line(faults);
	ReliablePacket near(line.a);
	GiveSession(near);
	ReliablePacket far(line.b);
	GiveSession(far);
	near.Process();
	far.Process();
	std::size_t sentNear = 0;
	std::size_t sentFar = 0;
	uint32 now = 0;
	while (now < 60000000) {
		now += 100;
		testNowUs = now;
		line.SetNow(now);
		if (sentNear < 40 && ReliableSend(near, static_cast<uint16>(sentNear)) == ResultStatus::ok) {
			sentNear++;
		}

		if (sentFar < 40 && ReliableSend(far, static_cast<uint16>(256 + sentFar)) == ResultStatus::ok) {
			sentFar++;
		}

		near.Process();
		far.Process();
		line.DeliverToB(far);
		line.DeliverToA(near);
		near.Process();
		far.Process();
		std::size_t settled = 0;
		for (std::size_t id = 0; id < 40; id++) {
			settled += testOutcomes[id] == 1 ? 1 : 0;
			settled += testOutcomes[256 + id] == 1 ? 1 : 0;
		}

		if (sentNear == 40 && sentFar == 40 && settled == 80) {
			break;
		}
	}

	for (std::size_t id = 0; id < 40; id++) {
		SIM_EXPECT(testDelivered[id] <= 1);
		SIM_EXPECT(testOutcomes[id] == 1);
		SIM_EXPECT(testDelivered[256 + id] <= 1);
		SIM_EXPECT(testOutcomes[256 + id] == 1);
	}

	SIM_EXPECT(testRestarts == 0);
}


int main() {
	TestStreamWithByteFaults();
	TestPacketsWithFaultsAndReordering();
	TestBothDirectionsWithFaults();
	return SimFinish("TestVubReliableFaults");
}
