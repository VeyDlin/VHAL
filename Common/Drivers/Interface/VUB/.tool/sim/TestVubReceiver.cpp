#include "SimCheck.h"
#include "Chains.h"
#include "Doubles.h"
#include <Drivers/Interface/VUB/Blocks.h>
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Receiver.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>
#include <type_traits>


// How a link hands received data to its stack: a receiver the stack gives it (SetReceiver, for a link that receives in an interrupt),
// or a receiver the link is polled with (Poll, for a link that reads on request). The byte stream is the FlagFraming frame
// 7E 01 02 03 03 7E, which is the packet {01, 02}.


// A byte-stream link that receives in an interrupt. The test plays the interrupt: it feeds the receiver the link was given.
struct InterruptStreamLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().MaxPacket(64).HalfDuplex().Starts(VUB::Initiator::Either);


	void SetReceiver(VUB::Receiver value) {
		receiver = value;
	}


	Result<std::size_t> Write(std::span<const uint8> bytes) {
		return bytes.size();
	}


	VUB::Receiver receiver;
};


// A packet link that receives in an interrupt.
struct InterruptPacketLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex();


	void SetReceiver(VUB::Receiver value) {
		receiver = value;
	}


	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}


	VUB::Receiver receiver;
};


// A byte-stream link that reads on request. Poll takes the bytes of the wire that the receiver has room for.
struct PolledStreamLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().MaxPacket(64).HalfDuplex().Starts(VUB::Initiator::Either);


	void Poll(VUB::Receiver receiver) {
		polls++;
		std::size_t take = wireLength - wireRead;
		if (receiver.Room() < take) {
			take = receiver.Room();
		}

		receiver.Feed(std::span<const uint8>(wire.data() + wireRead, take));
		wireRead += take;
	}


	Result<std::size_t> Write(std::span<const uint8> bytes) {
		return bytes.size();
	}


	std::array<uint8, 64> wire{};
	std::size_t wireLength = 0;
	std::size_t wireRead = 0;
	std::size_t polls = 0;
};


// A packet link that reads on request. Poll feeds packets while the receiver has room.
struct PolledPacketLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex();


	void Poll(VUB::Receiver receiver) {
		polls++;
		while (next < packets.size() && receiver.Room() > 0) {
			receiver.FeedPacket(std::span<const uint8>(packets[next]));
			next++;
		}
	}


	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}


	std::array<std::array<uint8, 2>, 3> packets{{{1, 2}, {3, 4}, {5, 6}}};
	std::size_t next = 0;
	std::size_t polls = 0;
};


// A link with neither member: the application feeds the stack itself.
struct PlainStreamLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().MaxPacket(64).HalfDuplex().Starts(VUB::Initiator::Either);


	Result<std::size_t> Write(std::span<const uint8> bytes) {
		return bytes.size();
	}
};


struct PlainPacketLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex();


	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}
};


using InterruptStream = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.Framing<FlagFraming>()
		.Link<InterruptStreamLink>()
		.MaxPayload(40)
		.ReceiveRing(16)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using InterruptPacket = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.Link<InterruptPacketLink>()
		.MaxPayload(40)
		.ReceiveQueue(2)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using PolledStream = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.Framing<FlagFraming>()
		.Link<PolledStreamLink>()
		.MaxPayload(40)
		.ReceiveRing(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using PolledPacket = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.Link<PolledPacketLink>()
		.MaxPayload(40)
		.ReceiveQueue(2)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// Chains of a link with neither SetReceiver nor Poll: the receiver adds nothing to their size.
using PlainPacketStack = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.Link<PlainPacketLink>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using PlainStreamStack = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.Framing<FlagFraming>()
		.Link<PlainStreamLink>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


static_assert(VUB::ReceivingLink<InterruptStreamLink>);
static_assert(VUB::ReceivingLink<InterruptPacketLink>);
static_assert(VUB::PollingLink<PolledStreamLink>);
static_assert(VUB::PollingLink<PolledPacketLink>);
static_assert(!VUB::ReceivingLink<PolledStreamLink>);
static_assert(!VUB::PollingLink<InterruptStreamLink>);
static_assert(!VUB::ReceivingLink<PlainStreamLink>);
static_assert(!VUB::PollingLink<PlainStreamLink>);
static_assert(!VUB::ReceivingLink<PlainPacketLink>);
static_assert(!VUB::PollingLink<PlainPacketLink>);

// A stack whose link receives in an interrupt gives the receiver back on destruction, so it cannot be copied.
static_assert(!std::is_copy_constructible_v<InterruptStream>);
static_assert(!std::is_copy_constructible_v<InterruptPacket>);

// A chain whose link has neither member has the size it had before the receiver: its storage is not changed.
static_assert(sizeof(PlainPacketStack) == 528);
static_assert(sizeof(PlainStreamStack) == 704);

static const std::array<uint8, 6> frame = {0x7E, 0x01, 0x02, 0x03, 0x03, 0x7E};


void CheckDefaultReceiver() {
	VUB::Receiver none;
	SIM_EXPECT(none.Room() == 0);
	none.Feed(std::span<const uint8>(frame));
	std::array<uint8, 2> packet = {0x01, 0x02};
	none.FeedPacket(std::span<const uint8>(packet));
	SIM_EXPECT(none.Room() == 0);
}


void CheckInterruptStream() {
	ResetTestState();
	InterruptStreamLink link;
	InterruptStream stack(link);
	SIM_EXPECT(link.receiver.Room() == 16);

	link.receiver.Feed(std::span<const uint8>(frame));
	SIM_EXPECT(link.receiver.Room() == 10);
	stack.Process();
	SIM_EXPECT(testReceivedCount == 1);
	SIM_EXPECT(testReceivedLength[0] == 2);
	SIM_EXPECT(testReceived[0][0] == 0x01 && testReceived[0][1] == 0x02);
	SIM_EXPECT(link.receiver.Room() == 16);

	std::array<uint8, 20> noise{};
	link.receiver.Feed(std::span<const uint8>(noise));
	SIM_EXPECT(link.receiver.Room() == 0);
	SIM_EXPECT(stack.Drops().ringFull == 4);
	stack.Process();
	SIM_EXPECT(link.receiver.Room() == 16);
}


void CheckInterruptPacket() {
	ResetTestState();
	InterruptPacketLink link;
	InterruptPacket stack(link);
	SIM_EXPECT(link.receiver.Room() == 2);

	std::array<uint8, 2> first = {0x01, 0x02};
	std::array<uint8, 2> second = {0x03, 0x04};
	std::array<uint8, 2> third = {0x05, 0x06};
	link.receiver.FeedPacket(std::span<const uint8>(first));
	SIM_EXPECT(link.receiver.Room() == 1);
	link.receiver.FeedPacket(std::span<const uint8>(second));
	SIM_EXPECT(link.receiver.Room() == 0);
	link.receiver.FeedPacket(std::span<const uint8>(third));
	SIM_EXPECT(stack.Drops().queueFull == 1);

	std::array<uint8, 65> big{};
	link.receiver.FeedPacket(std::span<const uint8>(big));
	SIM_EXPECT(stack.Drops().tooLong == 1);
	SIM_EXPECT(link.receiver.Room() == 0);

	// A packet link has no byte ring: its receiver drops bytes, and the queue is not changed.
	link.receiver.Feed(frame);
	SIM_EXPECT(link.receiver.Room() == 0);
	SIM_EXPECT(stack.Drops().ringFull == 0);

	stack.Process();
	SIM_EXPECT(testReceivedCount == 2);
	SIM_EXPECT(testReceived[1][0] == 0x03);
	SIM_EXPECT(link.receiver.Room() == 2);
}


// The wire holds one frame. The ring holds four bytes, so the first poll takes four and the second the last two.
void CheckPolledStream() {
	ResetTestState();
	PolledStreamLink link;
	for (std::size_t index = 0; index < frame.size(); index++) {
		link.wire[index] = frame[index];
	}

	link.wireLength = frame.size();
	PolledStream stack(link);

	stack.Process();
	SIM_EXPECT(link.polls == 1);
	SIM_EXPECT(link.wireRead == 4);
	SIM_EXPECT(testReceivedCount == 0);

	stack.Process();
	SIM_EXPECT(link.wireRead == 6);
	SIM_EXPECT(testReceivedCount == 1);
	SIM_EXPECT(testReceived[0][0] == 0x01 && testReceived[0][1] == 0x02);

	stack.Process();
	SIM_EXPECT(link.polls == 3);
	SIM_EXPECT(testReceivedCount == 1);
	SIM_EXPECT(stack.Drops().ringFull == 0);
}


// The link feeds two packets on the first poll and the third on the second: each is handled in the Process that polls it.
void CheckPolledPacket() {
	ResetTestState();
	PolledPacketLink link;
	PolledPacket stack(link);

	stack.Process();
	SIM_EXPECT(link.polls == 1);
	SIM_EXPECT(testReceivedCount == 2);

	stack.Process();
	SIM_EXPECT(testReceivedCount == 3);
	SIM_EXPECT(testReceivedLength[2] == 2);
	SIM_EXPECT(testReceived[2][0] == 0x05);
	SIM_EXPECT(stack.Drops().queueFull == 0);
}


// Destruction gives the receiver back: the link holds a null receiver, which reports no room and drops what it is given.
void CheckDestructionTakesBack() {
	ResetTestState();
	InterruptStreamLink streamLink;
	{
		InterruptStream stack(streamLink);
		SIM_EXPECT(streamLink.receiver.Room() == 16);
	}

	SIM_EXPECT(streamLink.receiver.Room() == 0);
	streamLink.receiver.Feed(std::span<const uint8>(frame));
	SIM_EXPECT(testReceivedCount == 0);

	InterruptPacketLink packetLink;
	{
		InterruptPacket stack(packetLink);
		SIM_EXPECT(packetLink.receiver.Room() == 2);
	}

	SIM_EXPECT(packetLink.receiver.Room() == 0);
}


int main() {
	CheckDefaultReceiver();
	CheckInterruptStream();
	CheckInterruptPacket();
	CheckPolledStream();
	CheckPolledPacket();
	CheckDestructionTakesBack();
	return SimFinish("TestVubReceiver");
}
