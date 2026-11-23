#include "SimCheck.h"
#include "VubChains.h"
#include <Drivers/Interface/VUB/TestKit/VubLoopback.h>
#include <Drivers/Interface/VUB/TestKit/VubWire.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <optional>
#include <span>


// Outages of the test kit's lines, and what the stacks do through them. A reporting line tells both ends; a silent one only loses data.


// A down wire loses what is pushed into it and counts it. What was pushed before it went down still arrives.
void TestDownWireLosesWhatIsSent() {
	VubFaults faults;
	VubStreamWire<128> wire(faults, 1);
	std::array<uint8, 4> bytes = {1, 2, 3, 4};
	wire.Push(std::span<const uint8>(bytes.data(), 2));
	wire.SetDown(true);
	wire.Push(std::span<const uint8>(bytes.data() + 2, 2));
	RecordSink sink;
	wire.Deliver(sink);
	SIM_EXPECT(sink.byteCount == 2);
	SIM_EXPECT(wire.Counts().outaged == 2);
	wire.SetDown(false);
	wire.Push(std::span<const uint8>(bytes.data(), 1));
	wire.Deliver(sink);
	SIM_EXPECT(sink.byteCount == 3);
	SIM_EXPECT(wire.Counts().outaged == 2);
}


// The same for a packet wire: a packet sent while it is down is lost and counted; one sent before it went down arrives whole.
void TestDownPacketWireLosesWhatIsSent() {
	VubFaults faults;
	VubPacketWire<16, 32> wire(faults, 1);
	std::array<uint8, 3> packet = {1, 2, 3};
	wire.Push(std::span<const uint8>(packet.data(), packet.size()));
	wire.SetDown(true);
	wire.Push(std::span<const uint8>(packet.data(), packet.size()));
	RecordSink sink;
	wire.Deliver(sink);
	SIM_EXPECT(sink.packetCount == 1);
	SIM_EXPECT(wire.Counts().outaged == 1);
}


// A line that reports tells both ends when it goes down and up, at the next Process of each.
void TestLineTellsBothEnds() {
	ResetTestState();
	ReportingPacketLine line(VubFaults{});
	ReportingPacketStack near(line.a);
	ReportingPacketStack far(line.b);
	near.Process();
	far.Process();
	SIM_EXPECT(near.LinkUp());
	SIM_EXPECT(far.LinkUp());
	line.SetLinkUp(false);
	near.Process();
	far.Process();
	SIM_EXPECT(!near.LinkUp());
	SIM_EXPECT(!far.LinkUp());
	line.SetLinkUp(true);
	near.Process();
	far.Process();
	SIM_EXPECT(near.LinkUp());
	SIM_EXPECT(far.LinkUp());
}


// Wait: packets queued while the link is down stay queued, Send queues until the queue is full and then returns busy, and after up the
// queued packets go in order. Nothing is counted as lost.
void TestWaitKeepsQueuedPackets() {
	ResetTestState();
	ReportingPacketLine line(VubFaults{});
	ReportingPacketStack sender(line.a);
	ReportingPacketStack receiver(line.b);
	sender.Process();
	receiver.Process();
	line.a.StartRound(0);
	std::array<uint8, 2> payload = {0, 0x33};
	for (std::size_t index = 0; index < 3; index++) {
		payload[0] = static_cast<uint8>(index);
		SIM_EXPECT(sender.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	}

	line.SetLinkUp(false);
	sender.Process();
	SIM_EXPECT(!sender.LinkUp());
	payload[0] = 3;
	SIM_EXPECT(sender.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	payload[0] = 4;
	SIM_EXPECT(sender.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::busy);

	line.SetLinkUp(true);
	line.a.StartRound(10);
	sender.Process();
	line.DeliverToB(receiver);
	receiver.Process();
	SIM_EXPECT(testReceivedCount == 4);
	for (std::size_t index = 0; index < 4 && index < testReceivedCount; index++) {
		SIM_EXPECT(testReceived[index][0] == index);
	}

	SIM_EXPECT(sender.Drops().linkDown == 0);
}


// Fail: the packets queued when the link goes down are dropped and counted as linkDown, Send returns noConnection while the link is
// down and does not count it, and a packet sent after up goes through.
void TestFailDropsQueuedPackets() {
	ResetTestState();
	ReportingPacketLine line(VubFaults{});
	FailingPacketStack sender(line.a);
	FailingPacketStack receiver(line.b);
	sender.Process();
	receiver.Process();
	line.a.StartRound(0);
	std::array<uint8, 2> payload = {1, 0x33};
	for (std::size_t index = 0; index < 3; index++) {
		SIM_EXPECT(sender.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	}

	line.SetLinkUp(false);
	sender.Process();
	SIM_EXPECT(sender.Drops().linkDown == 3);
	SIM_EXPECT(sender.LinkDowns() == 1);
	SIM_EXPECT(sender.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::noConnection);
	SIM_EXPECT(sender.Drops().linkDown == 3);

	line.SetLinkUp(true);
	sender.Process();
	SIM_EXPECT(sender.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	line.a.StartRound(5);
	sender.Process();
	line.DeliverToB(receiver);
	receiver.Process();
	SIM_EXPECT(testReceivedCount == 1);
	SIM_EXPECT(testReceived[0][0] == 1);
	SIM_EXPECT(sender.Drops().linkDown == 3);
}


// Wait, with a partial frame in the receiver: the frame half received before the outage is dropped by the framing's Reset, and the
// sender writes its frame again from its first byte after up. The first frame after up arrives whole, with no framing error.
void TestWaitResetsHalfReceivedFrame() {
	ResetTestState();
	ReportingStreamLine line(VubFaults{});
	ReportingLengthStack sender(line.a);
	ReportingLengthStack receiver(line.b);
	sender.Process();
	receiver.Process();
	std::array<uint8, 4> first = {0x11, 0x22, 0x33, 0x44};
	SIM_EXPECT(sender.Send(std::span<const uint8>(first.data(), first.size())) == ResultStatus::ok);
	line.a.StartRound(3);
	sender.Process();
	line.DeliverToB(receiver);
	receiver.Process();
	SIM_EXPECT(testReceivedCount == 0);

	line.SetLinkUp(false);
	receiver.Process();
	sender.Process();
	SIM_EXPECT(!receiver.LinkUp());

	line.SetLinkUp(true);
	line.a.StartRound(100);
	receiver.Process();
	sender.Process();
	line.DeliverToB(receiver);
	receiver.Process();
	SIM_EXPECT(testReceivedCount == 1);
	SIM_EXPECT(testReceivedLength[0] == 4);
	for (std::size_t index = 0; index < 4; index++) {
		SIM_EXPECT(testReceived[0][index] == first[index]);
	}

	SIM_EXPECT(receiver.Drops().framingError == 0);
}


// Fail, with a partial frame: the frame the sender was writing is dropped and counted as linkDown, and the receiver's partial frame is
// reset. A frame sent after up arrives whole.
void TestFailDropsHalfWrittenFrame() {
	ResetTestState();
	ReportingStreamLine line(VubFaults{});
	FailingLengthStack sender(line.a);
	FailingLengthStack receiver(line.b);
	sender.Process();
	receiver.Process();
	std::array<uint8, 4> first = {0x11, 0x22, 0x33, 0x44};
	SIM_EXPECT(sender.Send(std::span<const uint8>(first.data(), first.size())) == ResultStatus::ok);
	line.a.StartRound(3);
	sender.Process();
	line.DeliverToB(receiver);
	receiver.Process();
	SIM_EXPECT(testReceivedCount == 0);

	line.SetLinkUp(false);
	receiver.Process();
	sender.Process();
	SIM_EXPECT(sender.Drops().linkDown == 1);

	line.SetLinkUp(true);
	receiver.Process();
	sender.Process();
	std::array<uint8, 2> second = {0x55, 0x66};
	SIM_EXPECT(sender.Send(std::span<const uint8>(second.data(), second.size())) == ResultStatus::ok);
	line.a.StartRound(100);
	sender.Process();
	line.DeliverToB(receiver);
	receiver.Process();
	SIM_EXPECT(testReceivedCount == 1);
	SIM_EXPECT(testReceivedLength[0] == 2);
	SIM_EXPECT(testReceived[0][0] == 0x55);
	SIM_EXPECT(testReceived[0][1] == 0x66);
}


// A silent line: the stacks never learn of the outage. Frames lost in it are counted by the framing, and the flag framing finds the
// frame boundaries again, so the frames sent after the outage arrive whole.
void TestSilentLineRecoversFraming() {
	ResetTestState();
	StreamLine line(VubFaults{});
	SilentStreamStack sender(line.a);
	SilentStreamStack receiver(line.b);
	for (std::size_t index = 0; index < 3; index++) {
		std::array<uint8, 4> payload = {static_cast<uint8>(index), 0x22, 0x33, 0x44};
		SIM_EXPECT(sender.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
		sender.Process();
		line.DeliverToB(receiver);
		receiver.Process();
	}

	SIM_EXPECT(testReceivedCount == 3);

	std::array<uint8, 7> cut = {0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB};
	SIM_EXPECT(sender.Send(std::span<const uint8>(cut.data(), cut.size())) == ResultStatus::ok);
	line.a.StartRound(5);
	sender.Process();
	line.DeliverToB(receiver);
	receiver.Process();

	line.SetLinkUp(false);
	line.a.StartRound(100);
	sender.Process();
	line.SetLinkUp(true);

	for (std::size_t index = 0; index < 5; index++) {
		std::array<uint8, 4> payload = {static_cast<uint8>(10 + index), 0x22, 0x33, 0x44};
		SIM_EXPECT(sender.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
		line.a.StartRound(100);
		sender.Process();
		line.DeliverToB(receiver);
		receiver.Process();
	}

	SIM_EXPECT(line.toB.Counts().outaged > 0);
	SIM_EXPECT(receiver.Drops().framingError == 1);
	SIM_EXPECT(testReceivedCount == 8);
	for (std::size_t index = 0; index < 5 && index + 3 < testReceivedCount; index++) {
		SIM_EXPECT(testReceived[index + 3][0] == 10 + index);
	}
}


// A scheduled outage from the seed, on a line that reports. Packets sent while it is down wait, nothing is lost on the line, every
// packet arrives once and in order, and the link went down more than once.
void TestScheduledOutagesLoseNothingWhenReported() {
	ResetTestState();
	VubFaults faults;
	faults.seed = 17;
	faults.outageEveryUs = 300;
	faults.outageForUs = 120;
	ReportingPacketLine line(faults);
	ReportingPacketStack sender(line.a);
	ReportingPacketStack receiver(line.b);
	const std::size_t total = 200;
	std::size_t sent = 0;
	for (uint32 step = 0; step < 20000 && (sent < total || testReceivedCount < total); step++) {
		uint32 now = step * 10;
		testNowUs = now;
		line.SetNow(now);
		if (sent < total) {
			std::array<uint8, 2> payload = {static_cast<uint8>(sent), 0x5A};
			if (sender.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok) {
				sent++;
			}
		}

		line.a.StartRound(2);
		sender.Process();
		line.DeliverToB(receiver);
		receiver.Process();
	}

	SIM_EXPECT(sent == total);
	SIM_EXPECT(testReceivedCount == total);
	for (std::size_t index = 0; index < total && index < testReceivedCount; index++) {
		SIM_EXPECT(testReceived[index][0] == index);
	}

	SIM_EXPECT(sender.LinkDowns() >= 2);
	SIM_EXPECT(line.toB.Counts().outaged == 0);
}


// The far stack is destroyed and built again in the middle of a run, on a line that does not report. The near stack keeps sending, and
// the rebuilt far stack takes the packets.
void TestPeerRebootSilentLine() {
	ResetTestState();
	PacketLine line(VubFaults{});
	PacketStack near(line.a);
	std::optional<PacketStack> far;
	far.emplace(line.b);
	std::array<uint8, 2> payload = {0x01, 0x02};
	SIM_EXPECT(near.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	SIM_EXPECT(near.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	near.Process();
	line.DeliverToB(*far);
	far->Process();
	SIM_EXPECT(testReceivedCount == 2);

	far.reset();
	payload[0] = 0x03;
	SIM_EXPECT(near.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	near.Process();

	far.emplace(line.b);
	payload[0] = 0x04;
	SIM_EXPECT(near.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	near.Process();
	line.DeliverToB(*far);
	far->Process();
	SIM_EXPECT(testReceivedCount == 4);
	SIM_EXPECT(testReceived[2][0] == 0x03);
	SIM_EXPECT(testReceived[3][0] == 0x04);
	SIM_EXPECT(near.Drops().sendError == 0);
}


// The same on a line that reports. The destroyed stack has taken its state sink back from the line, so the outage that comes while no
// far stack exists reports into nothing, and the rebuilt far stack reports up when it is built.
void TestPeerRebootReportingLine() {
	ResetTestState();
	ReportingPacketLine line(VubFaults{});
	ReportingPacketStack near(line.a);
	std::optional<ReportingPacketStack> far;
	far.emplace(line.b);
	near.Process();
	far->Process();

	std::array<uint8, 2> payload = {0x01, 0x02};
	SIM_EXPECT(near.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	near.Process();
	line.DeliverToB(*far);
	far->Process();
	SIM_EXPECT(testReceivedCount == 1);

	far.reset();
	line.SetLinkUp(false);
	near.Process();
	SIM_EXPECT(!near.LinkUp());

	line.SetLinkUp(true);
	far.emplace(line.b);
	far->Process();
	near.Process();
	SIM_EXPECT(near.LinkUp());

	payload[0] = 0x03;
	SIM_EXPECT(near.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	near.Process();
	line.DeliverToB(*far);
	far->Process();
	SIM_EXPECT(testReceivedCount == 2);
	SIM_EXPECT(testReceived[1][0] == 0x03);
}


// A down discards the bytes of the receive ring: they are a partial frame, and useless after a disconnect. The frame that comes after up is
// not joined to them, and the bytes are counted as linkDown.
void TestDownDiscardsReceiveRing() {
	ResetTestState();
	ReportingStreamLine line(VubFaults{});
	ReportingLengthStack sender(line.a);
	ReportingLengthStack receiver(line.b);
	sender.Process();
	receiver.Process();
	receiver.Feed(0x04);
	receiver.Feed(0x11);
	line.SetLinkUp(false);
	receiver.Process();
	SIM_EXPECT(receiver.Drops().linkDown == 2);

	line.SetLinkUp(true);
	receiver.Process();
	std::array<uint8, 2> payload = {0x55, 0x66};
	SIM_EXPECT(sender.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	line.a.StartRound(100);
	sender.Process();
	line.DeliverToB(receiver);
	receiver.Process();
	SIM_EXPECT(testReceivedCount == 1);
	SIM_EXPECT(testReceivedLength[0] == 2);
	SIM_EXPECT(testReceived[0][0] == 0x55);
	SIM_EXPECT(testReceived[0][1] == 0x66);
}


// A down keeps the packets the receive queue holds: they are whole, and they still go up.
void TestDownKeepsReceivedPackets() {
	ResetTestState();
	ReportingPacketLine line(VubFaults{});
	ReportingPacketStack receiver(line.b);
	receiver.Process();
	std::array<uint8, 2> first = {0x01, 0x02};
	std::array<uint8, 2> second = {0x03, 0x04};
	receiver.FeedPacket(std::span<const uint8>(first.data(), first.size()));
	receiver.FeedPacket(std::span<const uint8>(second.data(), second.size()));
	line.SetLinkUp(false);
	receiver.Process();
	SIM_EXPECT(testReceivedCount == 2);
	SIM_EXPECT(testReceived[0][0] == 0x01);
	SIM_EXPECT(testReceived[1][0] == 0x03);
	SIM_EXPECT(receiver.Drops().linkDown == 0);
}


// The faults of a line with a schedule of outages: down for 500 microseconds, then up for up to 1000.
VubFaults ScheduledFaults() {
	VubFaults faults;
	faults.outageEveryUs = 1000;
	faults.outageForUs = 500;
	return faults;
}


// A line taken down by hand stays down through SetNow, with or without a schedule. A packet or byte sent while it is down is lost. Once
// the line is taken up by hand, and the schedule is up at the same time, the line carries what is sent again.
template<typename Line, typename Stack>
void CheckManualDownHoldsThroughSetNow(const VubFaults& faults) {
	ResetTestState();
	Line line(faults);
	Stack sender(line.a);
	std::array<uint8, 2> payload = {0x01, 0x02};
	line.SetLinkUp(false);
	line.SetNow(0);
	SIM_EXPECT(sender.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	sender.Process();
	SIM_EXPECT(line.toB.Counts().outaged > 0);
	SIM_EXPECT(line.toB.Counts().delivered == 0);

	line.SetNow(5000);
	line.SetNow(6000);
	uint32 lostBefore = line.toB.Counts().outaged;
	SIM_EXPECT(sender.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	sender.Process();
	SIM_EXPECT(line.toB.Counts().outaged > lostBefore);
	SIM_EXPECT(line.toB.Counts().delivered == 0);

	line.SetLinkUp(true);
	line.SetNow(6000);
	SIM_EXPECT(sender.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	sender.Process();
	RecordSink sink;
	line.DeliverToB(sink);
	SIM_EXPECT(line.toB.Counts().delivered > 0);
}


int main() {
	TestDownWireLosesWhatIsSent();
	TestDownPacketWireLosesWhatIsSent();
	TestLineTellsBothEnds();
	TestWaitKeepsQueuedPackets();
	TestFailDropsQueuedPackets();
	TestWaitResetsHalfReceivedFrame();
	TestFailDropsHalfWrittenFrame();
	TestDownDiscardsReceiveRing();
	TestDownKeepsReceivedPackets();
	TestSilentLineRecoversFraming();
	TestScheduledOutagesLoseNothingWhenReported();
	TestPeerRebootSilentLine();
	TestPeerRebootReportingLine();
	CheckManualDownHoldsThroughSetNow<PacketLine, PacketStack>(VubFaults{});
	CheckManualDownHoldsThroughSetNow<PacketLine, PacketStack>(ScheduledFaults());
	CheckManualDownHoldsThroughSetNow<StreamLine, StreamStack>(VubFaults{});
	CheckManualDownHoldsThroughSetNow<StreamLine, StreamStack>(ScheduledFaults());
	return SimFinish("TestVubOutage");
}
