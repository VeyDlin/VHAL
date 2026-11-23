#include "SimCheck.h"
#include "Chains.h"
#include "Doubles.h"
#include <Drivers/Interface/VUB/Layer/KeepAlive.h>
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>


// The keep-alive layer on a silent packet line: the link does not report its state, so the layer is the only source of the peer's state.
// Every chain here has a quiet time of 1000 us and a down time of 3000 us. The runs step the clock by 100 us, so every time below is a
// multiple of 100.
using KeepAliveStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::KeepAlive>()
		.QuietTime(1000)
		.DownTime(3000)
		.Link<PacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The same chain under WhileDown(Fail): the application's queued packets are dropped on a down, and Send is refused while it is down.
using KeepAliveFailStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::KeepAlive>()
		.QuietTime(1000)
		.DownTime(3000)
		.Link<PacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.WhileDown(VUB::Down::Fail)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The same chain with the default times: a quiet time of one second and a down time of three seconds.
using KeepAliveDefaultStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::KeepAlive>()
		.Link<PacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The packets a line delivered to one end, with their bytes.
struct PacketLog {
	std::array<std::array<uint8, 64>, 16> bytes{};
	std::array<std::size_t, 16> length{};
	std::size_t count = 0;


	void FeedPacket(std::span<const uint8> packet) {
		if (count < length.size()) {
			for (std::size_t index = 0; index < packet.size(); index++) {
				bytes[count][index] = packet[index];
			}

			length[count] = packet.size();
		}

		count++;
	}
};


// One step of a run with two ends: the clock and the line's schedule move to nowUs, what is due reaches each end, and each end runs.
void TickPair(PacketLine& line, KeepAliveStack& a, KeepAliveStack& b, uint32 nowUs) {
	testNowUs = nowUs;
	line.SetNow(nowUs);
	line.DeliverToA(a);
	line.DeliverToB(b);
	a.Process();
	b.Process();
}


// The peer of a test that is not a stack. It counts the packets it gets, and answers each ping (the byte 0x01) with a pong (0x02).
template<typename End>
struct PongingPeer {
	End* end = nullptr;
	uint32 pings = 0;
	uint32 others = 0;


	void FeedPacket(std::span<const uint8> packet) {
		if (packet.size() == 1 && packet[0] == 0x01) {
			pings++;
			std::array<uint8, 1> pong = {0x02};
			(void)end->Send(std::span<const uint8>(pong.data(), pong.size()));
		} else {
			others++;
		}
	}
};


// One end with no peer. Nothing is received, so the peer is declared down at 3000 us, and the pings go at 1000 us and 2000 us before
// that, and at 3000 us with the down. A packet received after the down (here a pong) declares the peer up again at once.
void TestDeclaresPeerDownAfterDownTime() {
	ResetTestState();
	PacketLine line(VUB::Faults{});
	KeepAliveStack a(line.a);
	for (uint32 now = 0; now < 3000; now += 100) {
		testNowUs = now;
		line.SetNow(now);
		a.Process();
		SIM_EXPECT(a.LinkUp());
	}

	testNowUs = 3000;
	line.SetNow(3000);
	a.Process();
	SIM_EXPECT(!a.LinkUp());
	SIM_EXPECT(a.LinkDowns() == 1);
	SIM_EXPECT(line.toB.Counts().pushed == 3);

	std::array<uint8, 1> pong = {0x02};
	a.FeedPacket(std::span<const uint8>(pong.data(), pong.size()));
	testNowUs = 3500;
	line.SetNow(3500);
	a.Process();
	SIM_EXPECT(a.LinkUp());
	SIM_EXPECT(a.LinkDowns() == 1);
}


// A dead peer: nothing comes back, ever. The peer is down from 3000 us on and stays down. The pings go at every quiet time, 1000 us, so
// there are 30 of them up to 30000 us.
void TestDeadPeerGoesDownAndStaysDown() {
	ResetTestState();
	PacketLine line(VUB::Faults{});
	KeepAliveStack a(line.a);
	for (uint32 now = 0; now <= 30000; now += 100) {
		testNowUs = now;
		line.SetNow(now);
		a.Process();
		if (now >= 3000) {
			SIM_EXPECT(!a.LinkUp());
		}
	}

	SIM_EXPECT(a.LinkDowns() == 1);
	SIM_EXPECT(line.toB.Counts().pushed == 30);
}


// A quiet peer that is alive: it answers each ping with a pong. The pong comes back two steps after the ping, so the next ping goes 1200
// us after the last one: the pings are at 1000 us, then every 1200 us, 16 of them up to 20000 us. The peer stays up all the time.
void TestQuietPeerGetsPingsAndStaysUp() {
	ResetTestState();
	PacketLine line(VUB::Faults{});
	KeepAliveStack a(line.a);
	PongingPeer<PacketEnd> peer;
	peer.end = &line.b;
	for (uint32 now = 0; now <= 20000; now += 100) {
		testNowUs = now;
		line.SetNow(now);
		line.DeliverToA(a);
		line.DeliverToB(peer);
		a.Process();
		SIM_EXPECT(a.LinkUp());
	}

	SIM_EXPECT(a.LinkDowns() == 0);
	SIM_EXPECT(peer.pings == 16);
	SIM_EXPECT(peer.others == 0);
	SIM_EXPECT(line.toB.Counts().pushed == 16);
}


// A busy peer: it sends a data packet every 500 us, so something arrives inside every quiet time. It never gets a ping, and the packets
// it sends all reach the handler: the sends at 500 us up to 19500 us, 39 of them, arrive by 20000 us.
void TestBusyPeerGetsNoPing() {
	ResetTestState();
	PacketLine line(VUB::Faults{});
	KeepAliveStack a(line.a);
	std::array<uint8, 2> data = {0x00, 0x31};
	for (uint32 now = 0; now <= 20000; now += 100) {
		testNowUs = now;
		line.SetNow(now);
		line.DeliverToA(a);
		a.Process();
		if (now > 0 && now % 500 == 0) {
			(void)line.b.Send(std::span<const uint8>(data.data(), data.size()));
		}

		SIM_EXPECT(a.LinkUp());
	}

	SIM_EXPECT(line.toB.Counts().pushed == 0);
	SIM_EXPECT(testReceivedCount == 39);
}


// Both ends declare the other down in an outage of the line, which lasts from 5000 us to 12000 us. Both are down from 8000 us at the
// latest, and no packet can reach either end then. While the peer is down each end pings every 1000 us: the last ping before the line
// returns is at 11800 us, so the first one after it goes at 12800 us, and it reaches the other end at 12900 us. Both ends are up at 12900
// us, and both were down once.
void TestBothEndsRecoverAfterAnOutage() {
	ResetTestState();
	PacketLine line(VUB::Faults{});
	KeepAliveStack a(line.a);
	KeepAliveStack b(line.b);
	for (uint32 now = 0; now <= 20000; now += 100) {
		if (now == 5000) {
			line.SetLinkUp(false);
		}

		if (now == 12000) {
			line.SetLinkUp(true);
		}

		TickPair(line, a, b, now);
		if (now >= 8100 && now < 12900) {
			SIM_EXPECT(!a.LinkUp());
			SIM_EXPECT(!b.LinkUp());
		}

		if (now >= 12900) {
			SIM_EXPECT(a.LinkUp());
			SIM_EXPECT(b.LinkUp());
		}
	}

	SIM_EXPECT(a.LinkDowns() == 1);
	SIM_EXPECT(b.LinkDowns() == 1);
}


// The application's packet waits while both ends are down under WhileDown(Wait): Send is accepted, the packet is not sent until the peer is
// up, and then it goes once. The outage is the same as in TestBothEndsRecoverAfterAnOutage.
void TestQueuedPacketWaitsWhileDownAndGoesAfterUp() {
	ResetTestState();
	PacketLine line(VUB::Faults{});
	KeepAliveStack a(line.a);
	KeepAliveStack b(line.b);
	std::array<uint8, 2> payload = {0x31, 0x32};
	for (uint32 now = 0; now <= 20000; now += 100) {
		if (now == 5000) {
			line.SetLinkUp(false);
		}

		if (now == 12000) {
			line.SetLinkUp(true);
		}

		TickPair(line, a, b, now);
		if (now == 9000) {
			SIM_EXPECT(a.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
		}

		if (now == 11900) {
			SIM_EXPECT(testReceivedCount == 0);
		}
	}

	SIM_EXPECT(testReceivedCount == 1);
	SIM_EXPECT(testReceivedLength[0] == 2);
	SIM_EXPECT(testReceived[0][0] == 0x31);
	SIM_EXPECT(testReceived[0][1] == 0x32);
	SIM_EXPECT(a.Drops().linkDown == 0);
}


// Under WhileDown(Fail) a packet the application queued before the peer went down is dropped and counted under linkDown, and Send is
// refused while the peer is down, without a count. The link refuses everything until 9000 us (a round budget of zero), so the packet
// stays queued; the peer is declared down at 3000 us, with nothing ever received.
void TestQueuedPacketIsRefusedAndCountedUnderFail() {
	ResetTestState();
	PacketLine line(VUB::Faults{});
	KeepAliveFailStack a(line.a);
	std::array<uint8, 2> payload = {0x31, 0x32};
	a.Process();
	line.a.StartRound(0);
	SIM_EXPECT(a.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	for (uint32 now = 100; now < 3000; now += 100) {
		testNowUs = now;
		line.SetNow(now);
		a.Process();
		SIM_EXPECT(a.LinkUp());
	}

	SIM_EXPECT(a.Drops().linkDown == 0);
	testNowUs = 3000;
	line.SetNow(3000);
	a.Process();
	SIM_EXPECT(!a.LinkUp());
	SIM_EXPECT(a.Drops().linkDown == 1);
	SIM_EXPECT(a.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::noConnection);
	SIM_EXPECT(a.Drops().linkDown == 1);

	line.a.StartRound(100);
	PacketLog log;
	for (uint32 now = 3100; now <= 4000; now += 100) {
		testNowUs = now;
		line.SetNow(now);
		a.Process();
		line.DeliverToB(log);
	}

	SIM_EXPECT(testReceivedCount == 0);
	for (std::size_t index = 0; index < log.count && index < log.length.size(); index++) {
		SIM_EXPECT(log.length[index] != 3);
	}
}


// Any packet that reaches the layer counts as life, even one the layer refuses. A packet of an unknown kind at 2500 us makes the down
// time run from 2500 us: the peer is still up at 3000 us, and is down at 5500 us.
void TestRefusedPacketCountsAsLife() {
	ResetTestState();
	PacketLine line(VUB::Faults{});
	KeepAliveStack a(line.a);
	std::array<uint8, 1> junk = {0x07};
	for (uint32 now = 0; now <= 5500; now += 100) {
		testNowUs = now;
		line.SetNow(now);
		if (now == 2500) {
			a.FeedPacket(std::span<const uint8>(junk.data(), junk.size()));
		}

		a.Process();
		if (now < 5500) {
			SIM_EXPECT(a.LinkUp());
		}
	}

	SIM_EXPECT(!a.LinkUp());
	SIM_EXPECT(a.Drops().layerError == 1);
}


// The bytes on the wire, written by hand. A data packet is 00 followed by the payload. A ping that arrives at 500 us is answered at once
// with 02, and it counts as life at 600 us. The next ping waits for the quiet time from that: it goes at 1600 us, as 01.
void TestWireBytesOfDataPingAndPong() {
	ResetTestState();
	PacketLine line(VUB::Faults{});
	KeepAliveStack a(line.a);
	PacketLog log;
	std::array<uint8, 2> payload = {0x31, 0x32};
	std::array<uint8, 1> ping = {0x01};
	SIM_EXPECT(a.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	for (uint32 now = 0; now <= 1700; now += 100) {
		testNowUs = now;
		line.SetNow(now);
		line.DeliverToA(a);
		line.DeliverToB(log);
		a.Process();
		if (now == 500) {
			(void)line.b.Send(std::span<const uint8>(ping.data(), ping.size()));
		}
	}

	SIM_EXPECT(log.count == 3);
	SIM_EXPECT(log.length[0] == 3);
	SIM_EXPECT(log.bytes[0][0] == 0x00);
	SIM_EXPECT(log.bytes[0][1] == 0x31);
	SIM_EXPECT(log.bytes[0][2] == 0x32);
	SIM_EXPECT(log.length[1] == 1);
	SIM_EXPECT(log.bytes[1][0] == 0x02);
	SIM_EXPECT(log.length[2] == 1);
	SIM_EXPECT(log.bytes[2][0] == 0x01);
	SIM_EXPECT(a.Drops().layerError == 0);
}


// The same chain over a reporting packet line, for the link's state to reach the layer.
using ReportingKeepAliveStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::KeepAlive>()
		.QuietTime(1000)
		.DownTime(3000)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// A quiet peer that answers pings, on a reporting line. The line is up, then down from 5000 us for ten quiet times, then up at 15000 us.
// While the link is down the layer makes no pings and does not declare the peer down, so the peer is up as soon as the link is up, and
// the first ping after the outage goes at 16000 us, one quiet time after the line returns. No two packets of the layer are on the wire
// closer than a quiet time, and no Process puts more than one on the wire. After the line returns the pings go at 16000, 17200, 18400
// and 19600 us: each one is answered, and the next one waits the round trip of 200 us and the quiet time.
void TestLinkDownPingsWaitAndNoBurstOnReportingLine() {
	ResetTestState();
	ReportingPacketLine line(VUB::Faults{});
	ReportingKeepAliveStack a(line.a);
	PongingPeer<ReportingPacketEnd> peer;
	peer.end = &line.b;
	std::size_t pushed = 0;
	std::size_t pushedAtDown = 0;
	uint32 lastPushUs = 0;
	for (uint32 now = 0; now <= 20000; now += 100) {
		testNowUs = now;
		line.SetNow(now);
		if (now == 5000) {
			line.SetLinkUp(false);
			pushedAtDown = line.toB.Counts().pushed;
		}

		if (now == 15000) {
			line.SetLinkUp(true);
		}

		line.DeliverToA(a);
		line.DeliverToB(peer);
		a.Process();
		std::size_t count = line.toB.Counts().pushed;
		if (now >= 5000 && now < 15000) {
			SIM_EXPECT(!a.LinkUp());
			SIM_EXPECT(count == pushedAtDown);
		}

		if (count != pushed) {
			SIM_EXPECT(count - pushed == 1);
			SIM_EXPECT(now - lastPushUs >= 1000);
			lastPushUs = now;
			pushed = count;
		}

		if (now == 15000) {
			SIM_EXPECT(a.LinkUp());
		}
	}

	SIM_EXPECT(a.LinkUp());
	SIM_EXPECT(a.LinkDowns() == 1);
	SIM_EXPECT(line.toB.Counts().pushed == 8);
}


// The packets the line took from end a, one step at a time. A step that takes more than one packet, or a packet closer than the quiet time
// to the one before, counts as a burst. The time of the first packet is kept.
struct PingWatch {
	std::size_t taken = 0;
	uint32 lastUs = 0;
	uint32 firstUs = 0;
	std::size_t bursts = 0;


	void Step(std::size_t count, uint32 nowUs) {
		if (count == taken) {
			return;
		}

		if (count - taken != 1) {
			bursts++;
		}

		if (taken == 0) {
			firstUs = nowUs;
		} else if (nowUs - lastUs < 1000) {
			bursts++;
		}

		lastUs = nowUs;
		taken = count;
	}
};


// The link is down before the first Process, and no change is reported, since the stack's connection starts down. The layer alone keeps
// the link quiet: nothing is taken from end a while the link is down, and the first ping goes at 11000 us, one quiet time after the link
// comes up at 10000 us. The peer answers the pings from then on.
void TestLinkDownFromStartSendsNothingUntilUp() {
	ResetTestState();
	ReportingPacketLine line(VUB::Faults{});
	ReportingKeepAliveStack a(line.a);
	PongingPeer<ReportingPacketEnd> peer;
	peer.end = &line.b;
	line.SetLinkUp(false);
	PingWatch watch;
	for (uint32 now = 0; now <= 20000; now += 100) {
		testNowUs = now;
		line.SetNow(now);
		if (now == 10000) {
			line.SetLinkUp(true);
		}

		line.DeliverToA(a);
		line.DeliverToB(peer);
		a.Process();
		std::size_t count = line.toB.Counts().pushed;
		watch.Step(count, now);
		if (now < 10000) {
			SIM_EXPECT(count == 0);
		}
	}

	SIM_EXPECT(watch.bursts == 0);
	SIM_EXPECT(watch.firstUs == 11000);
	SIM_EXPECT(peer.pings >= 2);
	SIM_EXPECT(a.LinkUp());
}


// The peer is declared down at 3000 us, with its pings at 1000, 2000, 3000 and 4000 us on the wire. The line falls at 4500 us, while the
// connection is already down, so no link down is counted. The peer is silent until the line returns at 10000 us, and then it answers the
// four pings it was sent. Nothing is taken from end a while the line is down, and the first ping after it goes at least one quiet time
// after the line is up. The peer is up again, and the connection is up.
void TestLinkDownAfterPeerDeclaredDownSendsNothing() {
	ResetTestState();
	ReportingPacketLine line(VUB::Faults{});
	ReportingKeepAliveStack a(line.a);
	PongingPeer<ReportingPacketEnd> peer;
	peer.end = &line.b;
	PingWatch watch;
	std::size_t pushedAtDown = 0;
	std::size_t pushedAtUp = 0;
	uint32 firstAfterUs = 0;
	for (uint32 now = 0; now <= 20000; now += 100) {
		testNowUs = now;
		line.SetNow(now);
		if (now == 4500) {
			line.SetLinkUp(false);
			pushedAtDown = line.toB.Counts().pushed;
		}

		if (now == 10000) {
			line.SetLinkUp(true);
			pushedAtUp = line.toB.Counts().pushed;
		}

		line.DeliverToA(a);
		if (now >= 10000) {
			line.DeliverToB(peer);
		}

		a.Process();
		std::size_t count = line.toB.Counts().pushed;
		watch.Step(count, now);
		if (now >= 4500 && now < 10000) {
			SIM_EXPECT(count == pushedAtDown);
		}

		if (now >= 10000 && firstAfterUs == 0 && count > pushedAtUp) {
			firstAfterUs = now;
		}
	}

	SIM_EXPECT(pushedAtDown == 4);
	SIM_EXPECT(watch.bursts == 0);
	SIM_EXPECT(firstAfterUs >= 11000);
	SIM_EXPECT(peer.pings >= 4);
	SIM_EXPECT(a.LinkUp());
	SIM_EXPECT(a.LinkDowns() == 1);
}


// The default times, a second for the quiet time and three for the down time: at 2999999 us the peer is still up, and at 3000000 us it is
// declared down.
void TestDefaultTimesAreOneAndThreeSeconds() {
	ResetTestState();
	PacketLine line(VUB::Faults{});
	KeepAliveDefaultStack a(line.a);
	testNowUs = 0;
	line.SetNow(0);
	a.Process();
	testNowUs = 2999999;
	line.SetNow(2999999);
	a.Process();
	SIM_EXPECT(a.LinkUp());
	testNowUs = 3000000;
	line.SetNow(3000000);
	a.Process();
	SIM_EXPECT(!a.LinkUp());
}


// Every snippet of the keep-alive wiki page compiles here. The snippet is copied from .wiki/Common/Drivers/Interface/VUB/Layer/KeepAlive.md.
namespace WikiKeepAlive {
// A board on a packet link that cannot report its state. Quiet for half a second, declared down after two seconds.
struct BoardUart {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex();


	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}
};


void OnBoardPacket(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
}


// The user's microsecond clock. A timer counter in a real device; a constant here.
uint32 BoardClock() {
	return 0;
}


using Board = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::KeepAlive>()
		.QuietTime(500000)
		.DownTime(2000000)
		.Link<BoardUart>()
		.MaxPayload(32)
		.OnReceive<&OnBoardPacket>()
		.Clock<&BoardClock>();
}>;


BoardUart boardUart;
Board board(boardUart);


void BoardLoop() {
	std::array<uint8, 2> hello = {0x31, 0x32};
	(void)board.Send(std::span<const uint8>(hello.data(), hello.size()));
	board.Process();
}
}


int main() {
	WikiKeepAlive::BoardLoop();
	SIM_EXPECT(WikiKeepAlive::board.Drops().sendError == 0);
	TestLinkDownFromStartSendsNothingUntilUp();
	TestLinkDownAfterPeerDeclaredDownSendsNothing();
	TestLinkDownPingsWaitAndNoBurstOnReportingLine();
	TestDeclaresPeerDownAfterDownTime();
	TestDeadPeerGoesDownAndStaysDown();
	TestQuietPeerGetsPingsAndStaysUp();
	TestBusyPeerGetsNoPing();
	TestBothEndsRecoverAfterAnOutage();
	TestQueuedPacketWaitsWhileDownAndGoesAfterUp();
	TestQueuedPacketIsRefusedAndCountedUnderFail();
	TestRefusedPacketCountsAsLife();
	TestWireBytesOfDataPingAndPong();
	TestDefaultTimesAreOneAndThreeSeconds();
	return SimFinish("TestVubKeepAlive");
}
