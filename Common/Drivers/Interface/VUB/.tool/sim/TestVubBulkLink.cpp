#include "SimCheck.h"
#include "LinkKit.h"
#include "UsbDoubles.h"
#include <Drivers/Interface/VUB/Blocks.h>
#include <Drivers/Interface/VUB/Builder.h>
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Link/BulkLink.h>
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


// The bulk link over a vendor bulk function in buffered mode (FakeBulk, with the same members). It is a byte stream like the CDC
// link, and its state is only whether the function is configured: a vendor function has no DTR.


using Wire = ByteQueue<32>;
using BulkDouble = FakeBulk<32>;
using BulkLinkType = VUB::BulkLink<BulkDouble>;


static_assert(VUB::StreamLink<BulkLinkType>);
static_assert(VUB::PollingLink<BulkLinkType>);
static_assert(VUB::ReportingLink<BulkLinkType>);
static_assert(!VUB::ReceivingLink<BulkLinkType>);
static_assert(!VUB::PacketLink<BulkLinkType>);
static_assert(BulkLinkType::traits.reliable);
static_assert(BulkLinkType::traits.reportsState);
static_assert(!BulkLinkType::traits.framed);
static_assert(BulkLinkType::traits.duplex == VUB::Duplex::Full);
static_assert(BulkLinkType::traits.maxPacket == 64);


struct StateRecord {
	bool up = false;
	uint32 reports = 0;
};


void RecordState(void* owner, bool up) {
	StateRecord* record = static_cast<StateRecord*>(owner);
	record->up = up;
	record->reports++;
}


// The state is up exactly while the function is configured. It is sampled in Poll, and the sink is told only of a change.
void TestStateFollowsConfiguration() {
	Wire ab;
	Wire ba;
	BulkDouble function(ab, ba);
	BulkLinkType link(function);
	StateRecord record;
	link.SetStateSink(VUB::StateSink(&record, &RecordState));
	SIM_EXPECT(record.reports == 1);
	SIM_EXPECT(!record.up);

	FeedRecord feed;
	VUB::Receiver receiver = RecordReceiver(feed);
	link.Poll(receiver);
	SIM_EXPECT(record.reports == 1);

	function.SetConfigured(true);
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


// Write returns what the ring takes, 0 when the function is not configured.
void TestWriteReturnsWhatTheRingTakes() {
	Wire ab;
	Wire ba;
	BulkDouble function(ab, ba);
	BulkLinkType link(function);
	std::array<uint8, 40> data{};

	SIM_EXPECT(link.Write(data).Value() == 0);
	function.SetConfigured(true);
	SIM_EXPECT(link.Write(data).Value() == 32);
	SIM_EXPECT(link.Write(data).Value() == 0);
	SIM_EXPECT(ab.Count() == 32);
}


// Poll reads up to the room, and no more.
void TestPollReadsUpToRoom() {
	Wire ab;
	Wire ba;
	BulkDouble writer(ab, ba);
	BulkDouble reader(ba, ab);
	writer.SetConfigured(true);
	reader.SetConfigured(true);
	BulkLinkType writerLink(writer);
	BulkLinkType readerLink(reader);

	std::array<uint8, 10> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	SIM_EXPECT(writerLink.Write(data).Value() == 10);

	FeedRecord feed;
	feed.room = 4;
	readerLink.Poll(RecordReceiver(feed));
	SIM_EXPECT(feed.byteCount == 4);
	SIM_EXPECT(ab.Count() == 6);

	feed.room = 0;
	readerLink.Poll(RecordReceiver(feed));
	SIM_EXPECT(feed.byteCount == 4);
	SIM_EXPECT(ab.Count() == 6);

	feed.room = 100;
	readerLink.Poll(RecordReceiver(feed));
	SIM_EXPECT(feed.byteCount == 10);
	SIM_EXPECT(feed.bytes[9] == 10);
}


using BulkStackA = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<BulkLinkType>()
		.MaxPayload(32)
		.OnReceive<&ReceiveAtA>();
}>;


using BulkStackB = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<BulkLinkType>()
		.MaxPayload(32)
		.OnReceive<&ReceiveAtB>();
}>;


// Two stacks over two bulk functions, with COBS. The queues are small, so packets are written in parts.
void TestStacksExchangeOverBulkWithFullQueues() {
	ResetLinkKit();
	Wire ab;
	Wire ba;
	BulkDouble functionA(ab, ba);
	BulkDouble functionB(ba, ab);
	functionA.SetConfigured(true);
	functionB.SetConfigured(true);
	BulkLinkType linkA(functionA);
	BulkLinkType linkB(functionB);
	BulkStackA stackA(linkA);
	BulkStackB stackB(linkB);

	for (uint8 id = 0; id < 3; id++) {
		std::array<uint8, 24> payload = LinkPayload(id);
		SIM_EXPECT(stackA.Send(payload) == ResultStatus::ok);
	}

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
}


// The delivery layer over the bulk link.
using ReliableBulkStackA = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40)
		.Framing<VUB::Cobs>()
		.Link<BulkLinkType>()
		.MaxPayload(40)
		.OnReceive<&ReceiveAtA>()
		.Clock<&LinkClock>();
}>;


using ReliableBulkStackB = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40)
		.Framing<VUB::Cobs>()
		.Link<BulkLinkType>()
		.MaxPayload(40)
		.OnReceive<&ReceiveAtB>()
		.Clock<&LinkClock>();
}>;


void TestReliableStackOverBulk() {
	ResetLinkKit();
	Wire ab;
	Wire ba;
	BulkDouble functionA(ab, ba);
	BulkDouble functionB(ba, ab);
	functionA.SetConfigured(true);
	functionB.SetConfigured(true);
	BulkLinkType linkA(functionA);
	BulkLinkType linkB(functionB);
	ReliableBulkStackA stackA(linkA);
	ReliableBulkStackB stackB(linkB);
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
	TestStateFollowsConfiguration();
	TestWriteReturnsWhatTheRingTakes();
	TestPollReadsUpToRoom();
	TestStacksExchangeOverBulkWithFullQueues();
	TestReliableStackOverBulk();
	return SimFinish("TestVubBulkLink");
}
