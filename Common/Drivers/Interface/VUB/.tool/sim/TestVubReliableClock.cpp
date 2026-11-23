#include "SimCheck.h"
#include "ReliableChains.h"
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>

// The timers of the layer across the wrap of the 32-bit clock. The clock starts at 0xFFFE0000, 8 seconds before it wraps, so the resend
// timeouts of the session fall after the wrap: the first resend is due at 0xFFFE0000 + 200000, which is 68928 after the wrap, and the
// timeout after the last resend at 0xFFFE0000 + 800000, which is 668928 after the wrap. A timer that compares the clock without the
// wrap resends at once, and the test fails.


// Starts the session at the clock's value: the first Send is busy, then one round carries the sync and its acknowledgement.
void StartSessionAt(PacketLine& line, ReliableWire& near, ReliableWire& far) {
	std::array<uint8, 2> trigger = {0x00, 0x00};
	SIM_EXPECT(near.Send(std::span<const uint8>(trigger.data(), trigger.size())) == ResultStatus::busy);
	near.Process();
	line.DeliverToB(far);
	far.Process();
	line.DeliverToA(near);
	near.Process();
}


void TestResendTimerCrossesTheClockWrap() {
	ResetReliableState();
	const uint32 start = 0xFFFE0000u;
	testNowUs = start;
	PacketLine line(VUB::Faults{});
	ReliableWire near(line.a);
	GiveSession(near);
	ReliableWire far(line.b);
	GiveSession(far);
	line.SetNow(start);
	near.Process();
	far.Process();
	StartSessionAt(line, near, far);
	SIM_EXPECT(ReliableSend(near, 9) == ResultStatus::ok);
	// The first transmission goes out at the start; the line drops every packet from here on, so only the timers send after it.
	ReliableCapture discard;
	near.Process();
	line.DeliverToB(discard);
	uint32 pushedBefore = line.toB.Counts().pushed;
	std::array<uint32, 8> sentAt{};
	std::size_t sent = 0;
	for (uint32 step = 1; step <= 800; step++) {
		uint32 now = start + step * 1000u;
		testNowUs = now;
		line.SetNow(now);
		near.Process();
		uint32 pushed = line.toB.Counts().pushed;
		if (pushed != pushedBefore && sent < sentAt.size()) {
			sentAt[sent] = step * 1000u;
			sent++;
			pushedBefore = pushed;
		}

		line.DeliverToB(discard);
	}

	SIM_EXPECT(sent == 3);
	if (sent == 3) {
		SIM_EXPECT(sentAt[0] == 200000);
		SIM_EXPECT(sentAt[1] == 400000);
		SIM_EXPECT(sentAt[2] == 600000);
	}

	SIM_EXPECT(testOutcomes[9] == 1);
	SIM_EXPECT(testStatus[9] == ResultStatus::retryExhausted);
	SIM_EXPECT(testOutcomeAtUs[9] == 668928u);
	SIM_EXPECT(testAttempts[9] == 4);
}


int main() {
	TestResendTimerCrossesTheClockWrap();
	return SimFinish("TestVubReliableClock");
}
