#include "SimCheck.h"
#include "LinkKit.h"
#include "UsbDoubles.h"
#include <Drivers/Interface/VUB/Blocks.h>
#include <Drivers/Interface/VUB/Builder.h>
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Link/CdcLink.h>
#include <Drivers/Interface/VUB/LinkState.h>
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Receiver.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>


// The CDC link over a CDC-ACM function (FakeCdc, with the same members). Two functions share two queues, one for each direction.


using Wire = ByteQueue<32>;
using CdcDouble = FakeCdc<32>;
using CdcLinkType = VUB::CdcLink<CdcDouble>;


static_assert(VUB::StreamLink<CdcLinkType>);
static_assert(VUB::PollingLink<CdcLinkType>);
static_assert(VUB::ReportingLink<CdcLinkType>);
static_assert(!VUB::ReceivingLink<CdcLinkType>);
static_assert(!VUB::PacketLink<CdcLinkType>);
static_assert(CdcLinkType::traits.reliable);
static_assert(CdcLinkType::traits.reportsState);
static_assert(!CdcLinkType::traits.framed);
static_assert(CdcLinkType::traits.duplex == VUB::Duplex::Full);
static_assert(CdcLinkType::traits.maxPacket == 64);


// What a state sink was told, as the stack's record of the link.
struct StateRecord {
	bool up = false;
	uint32 reports = 0;
	// The first reports in the order they came: history[0] is the first report.
	bool history[8] = { false };
};


void RecordState(void* owner, bool up) {
	StateRecord* record = static_cast<StateRecord*>(owner);
	record->up = up;
	if (record->reports < 8) {
		record->history[record->reports] = up;
	}
	record->reports++;
}


// Write returns how many bytes the function queued: fewer when the ring fills, and 0 when the function is not configured.
void TestWriteReturnsWhatTheRingTakes() {
	Wire ab;
	Wire ba;
	CdcDouble function(ab, ba);
	CdcLinkType link(function);
	std::array<uint8, 40> data{};

	SIM_EXPECT(link.Write(data).Value() == 0);
	function.SetConfigured(true);
	SIM_EXPECT(link.Write(data).Value() == 32);
	SIM_EXPECT(link.Write(data).Value() == 0);
	SIM_EXPECT(ab.Count() == 32);
}


// The state is up while the function is configured and DTR is set. It is sampled in Poll, and the sink is told only of a change.
void TestStateIsUpWhileConfiguredAndTerminalOpen() {
	Wire ab;
	Wire ba;
	CdcDouble function(ab, ba);
	CdcLinkType link(function);
	StateRecord record;
	link.SetStateSink(VUB::StateSink(&record, &RecordState));
	SIM_EXPECT(record.reports == 1);
	SIM_EXPECT(!record.up);

	FeedRecord feed;
	VUB::Receiver receiver = RecordReceiver(feed);
	function.SetConfigured(true);
	link.Poll(receiver);
	SIM_EXPECT(!record.up);
	SIM_EXPECT(record.reports == 1);

	function.SetTerminalOpen(true);
	link.Poll(receiver);
	SIM_EXPECT(record.up);
	SIM_EXPECT(record.reports == 2);

	link.Poll(receiver);
	SIM_EXPECT(record.reports == 2);

	function.SetConfigured(false);
	link.Poll(receiver);
	SIM_EXPECT(!record.up);
	SIM_EXPECT(record.reports == 3);
}


// A session that ends and starts again between two Polls is a fall and a rise, in that order, although the state is up at both
// Polls. A session change while the link is down is no event.
void TestSessionChangeBetweenPollsIsAFallAndARise() {
	Wire ab;
	Wire ba;
	CdcDouble function(ab, ba);
	CdcLinkType link(function);
	StateRecord record;
	link.SetStateSink(VUB::StateSink(&record, &RecordState));
	FeedRecord feed;
	VUB::Receiver receiver = RecordReceiver(feed);
	function.SetConfigured(true);
	function.SetTerminalOpen(true);
	link.Poll(receiver);
	SIM_EXPECT(record.reports == 2);
	SIM_EXPECT(record.up);

	function.StartNewSession();
	link.Poll(receiver);
	SIM_EXPECT(record.reports == 4);
	SIM_EXPECT(record.history[2] == false);
	SIM_EXPECT(record.history[3] == true);
	SIM_EXPECT(record.up);

	function.SetConfigured(false);
	link.Poll(receiver);
	SIM_EXPECT(record.reports == 5);
	SIM_EXPECT(!record.up);

	function.StartNewSession();
	link.Poll(receiver);
	SIM_EXPECT(record.reports == 5);
	SIM_EXPECT(!record.up);
}


// Poll reads up to the receiver's room, and no more. A room of 0 reads nothing and leaves the bytes queued.
void TestPollReadsUpToRoom() {
	Wire ab;
	Wire ba;
	CdcDouble writer(ab, ba);
	CdcDouble reader(ba, ab);
	writer.SetConfigured(true);
	reader.SetConfigured(true);
	CdcLinkType writerLink(writer);
	CdcLinkType readerLink(reader);

	std::array<uint8, 12> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
	SIM_EXPECT(writerLink.Write(data).Value() == 12);

	FeedRecord feed;
	feed.room = 0;
	readerLink.Poll(RecordReceiver(feed));
	SIM_EXPECT(feed.byteCount == 0);
	SIM_EXPECT(ab.Count() == 12);

	feed.room = 5;
	readerLink.Poll(RecordReceiver(feed));
	SIM_EXPECT(feed.byteCount == 5);
	SIM_EXPECT(ab.Count() == 7);

	feed.room = 100;
	readerLink.Poll(RecordReceiver(feed));
	SIM_EXPECT(feed.byteCount == 12);
	SIM_EXPECT(feed.bytes[0] == 1);
	SIM_EXPECT(feed.bytes[11] == 12);
	SIM_EXPECT(ab.Count() == 0);
}


// The stack sees the state of the function: up once DTR is set, and its LinkUp() follows.
using CdcPlainStackA = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Framing<VUB::Cobs>()
		.Link<CdcLinkType>()
		.MaxPayload(32)
		.OnReceive<&ReceiveAtA>();
}>;


void TestStackFollowsTheFunctionState() {
	ResetLinkKit();
	Wire ab;
	Wire ba;
	CdcDouble function(ab, ba);
	CdcLinkType link(function);
	CdcPlainStackA stack(link);
	SIM_EXPECT(!stack.LinkUp());

	function.SetConfigured(true);
	stack.Process();
	SIM_EXPECT(!stack.LinkUp());

	function.SetTerminalOpen(true);
	stack.Process();
	SIM_EXPECT(stack.LinkUp());
	SIM_EXPECT(stack.LinkDowns() == 0);

	function.SetTerminalOpen(false);
	stack.Process();
	SIM_EXPECT(!stack.LinkUp());
	SIM_EXPECT(stack.LinkDowns() == 1);
}


// Two stacks, one on each function, with COBS. The queues are 32 bytes, so a packet is written in parts, and the rest waits in the
// stack until the peer has read what is queued.
using CdcStackA = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Framing<VUB::Cobs>()
		.Link<CdcLinkType>()
		.MaxPayload(32)
		.OnReceive<&ReceiveAtA>();
}>;


using CdcStackB = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Framing<VUB::Cobs>()
		.Link<CdcLinkType>()
		.MaxPayload(32)
		.OnReceive<&ReceiveAtB>();
}>;


void TestStacksExchangeOverCdcWithFullQueues() {
	ResetLinkKit();
	Wire ab;
	Wire ba;
	CdcDouble functionA(ab, ba);
	CdcDouble functionB(ba, ab);
	functionA.SetConfigured(true);
	functionA.SetTerminalOpen(true);
	functionB.SetConfigured(true);
	functionB.SetTerminalOpen(true);
	CdcLinkType linkA(functionA);
	CdcLinkType linkB(functionB);
	CdcStackA stackA(linkA);
	CdcStackB stackB(linkB);

	for (uint8 id = 0; id < 3; id++) {
		std::array<uint8, 24> payload = LinkPayload(id);
		SIM_EXPECT(stackA.Send(payload) == ResultStatus::ok);
	}

	stackA.Process();
	SIM_EXPECT(ab.Count() == 32);

	for (int round = 0; round < 40; round++) {
		stackA.Process();
		stackB.Process();
	}

	SIM_EXPECT(logAtB.count == 3);
	for (uint8 id = 0; id < 3; id++) {
		std::array<uint8, 24> payload = LinkPayload(id);
		SIM_EXPECT(logAtB.Is(id, payload));
	}

	SIM_EXPECT(stackB.Drops().ringFull == 0);
	SIM_EXPECT(stackA.Drops().sendError == 0);
}


// The delivery layer over the CDC link, with the same queues.
using ReliableCdcStackA = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40)
		.Framing<VUB::Cobs>()
		.Link<CdcLinkType>()
		.MaxPayload(40)
		.OnReceive<&ReceiveAtA>()
		.Clock<&LinkClock>();
}>;


using ReliableCdcStackB = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40)
		.Framing<VUB::Cobs>()
		.Link<CdcLinkType>()
		.MaxPayload(40)
		.OnReceive<&ReceiveAtB>()
		.Clock<&LinkClock>();
}>;


void TestReliableStackOverCdc() {
	ResetLinkKit();
	Wire ab;
	Wire ba;
	CdcDouble functionA(ab, ba);
	CdcDouble functionB(ba, ab);
	functionA.SetConfigured(true);
	functionA.SetTerminalOpen(true);
	functionB.SetConfigured(true);
	functionB.SetTerminalOpen(true);
	CdcLinkType linkA(functionA);
	CdcLinkType linkB(functionB);
	ReliableCdcStackA stackA(linkA);
	ReliableCdcStackB stackB(linkB);
	stackA.Find<VUB::ReliableOf<40, 4>>().SetSession(0x1234);
	stackB.Find<VUB::ReliableOf<40, 4>>().SetSession(0x1234);

	for (uint8 id = 0; id < 4; id++) {
		std::array<uint8, 24> payload = LinkPayload(id);
		int attempts = 0;
		while (stackA.Send(payload) != ResultStatus::ok && attempts < 100) {
			stackA.Process();
			stackB.Process();
			linkNowUs += 1000;
			attempts++;
		}

		SIM_EXPECT(attempts < 100);
	}

	for (int round = 0; round < 200; round++) {
		stackA.Process();
		stackB.Process();
		linkNowUs += 1000;
	}

	SIM_EXPECT(logAtB.count == 4);
	for (uint8 id = 0; id < 4; id++) {
		std::array<uint8, 24> payload = LinkPayload(id);
		SIM_EXPECT(logAtB.Is(id, payload));
	}
}


int main() {
	TestWriteReturnsWhatTheRingTakes();
	TestStateIsUpWhileConfiguredAndTerminalOpen();
	TestSessionChangeBetweenPollsIsAFallAndARise();
	TestPollReadsUpToRoom();
	TestStackFollowsTheFunctionState();
	TestStacksExchangeOverCdcWithFullQueues();
	TestReliableStackOverCdc();
	return SimFinish("TestVubCdcLink");
}
