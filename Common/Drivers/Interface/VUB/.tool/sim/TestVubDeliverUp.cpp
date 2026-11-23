#include "SimCheck.h"
#include "Chains.h"
#include "Doubles.h"
#include <Drivers/Interface/VUB/Blocks.h>
#include <Drivers/Interface/VUB/Builder.h>
#include <Drivers/Interface/VUB/Outbox.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>


// A layer hands a packet up from its Process: the packet goes through the blocks above the layer, as a packet the layer had just taken,
// and on to the handler. The hand-written wire bytes below come from XorTransform: one payload byte p is sent as p XOR 0xA5, then the
// sum and the XOR of that byte (both equal to it), so the body of payload p is three copies of p XOR 0xA5. The layer HoldLayer puts its
// sequence number in front of each packet, so a packet with sequence s and payload p is [s, p ^ 0xA5, p ^ 0xA5, p ^ 0xA5].


// The layer above HoldLayer. Its Take records the first byte of each packet it is given, in order.
inline std::array<uint8, 8> testSeen{};
inline std::size_t testSeenCount = 0;


struct RecordLayer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x52454331;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size()) {
			return ResultStatus::invalidParameter;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = in[index];
		}

		return in.size();
	}


	Result<VUB::Taken> Take(const VUB::Meta&, std::span<const uint8> packet) {
		if (packet.empty()) {
			return ResultStatus::invalidData;
		}

		if (testSeenCount < testSeen.size()) {
			testSeen[testSeenCount] = packet[0];
		}

		testSeenCount++;
		return VUB::Taken{0, true};
	}


	void Process(uint32, const VUB::Outbox&) {
	}
};


// A layer that delivers packets in order. Each packet it sends starts with a sequence number. A packet that comes in order goes up at
// once; a packet that comes early is held, and Process releases the held packets as the gap fills.
struct HoldLayer {
	static constexpr std::size_t header = 1;
	static constexpr uint32 fingerprint = 0x484F4C31;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;
	static constexpr bool passesUp = true;
	static constexpr std::size_t Slots = 4;
	static constexpr std::size_t SlotBytes = 8;


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size() + header) {
			return ResultStatus::invalidParameter;
		}

		out[0] = sequence;
		sequence++;
		for (std::size_t index = 0; index < in.size(); index++) {
			out[header + index] = in[index];
		}

		return in.size() + header;
	}


	Result<VUB::Taken> Take(const VUB::Meta&, std::span<const uint8> packet) {
		if (packet.empty()) {
			return ResultStatus::invalidData;
		}

		uint8 number = packet[0];
		if (number == expected) {
			expected++;
			return VUB::Taken{header, true};
		}

		for (std::size_t slot = 0; slot < Slots; slot++) {
			if (!used[slot]) {
				std::size_t length = packet.size() - header;
				for (std::size_t index = 0; index < length; index++) {
					bytes[slot][index] = packet[header + index];
				}

				lengths[slot] = length;
				numbers[slot] = number;
				used[slot] = true;
				break;
			}
		}

		return VUB::Taken{header, false};
	}


	void Process(uint32, const VUB::Outbox& outbox) {
		bool released = true;
		while (released) {
			released = false;
			for (std::size_t slot = 0; slot < Slots; slot++) {
				if (used[slot] && numbers[slot] == expected) {
					(void)outbox.Deliver(std::span<const uint8>(bytes[slot].data(), lengths[slot]));
					used[slot] = false;
					expected++;
					released = true;
				}
			}
		}
	}


	uint8 sequence = 0;
	uint8 expected = 0;
	std::array<std::array<uint8, SlotBytes>, Slots> bytes{};
	std::array<std::size_t, Slots> lengths{};
	std::array<uint8, Slots> numbers{};
	std::array<bool, Slots> used{};
};


// A layer that does not declare passesUp. Its Process tries to deliver one packet, and the status is kept.
inline ResultStatus testPlainDeliverStatus = ResultStatus::ok;


struct PlainUpLayer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x504C4E31;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size()) {
			return ResultStatus::invalidParameter;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = in[index];
		}

		return in.size();
	}


	Result<VUB::Taken> Take(const VUB::Meta&, std::span<const uint8>) {
		return VUB::Taken{0, true};
	}


	void Process(uint32, const VUB::Outbox& outbox) {
		std::array<uint8, 1> packet = {0x07};
		testPlainDeliverStatus = outbox.Deliver(std::span<const uint8>(packet.data(), packet.size()));
	}
};


// A layer that tries to deliver a packet one byte longer than the receive buffer of its link.
struct TooLongUpLayer : PlainUpLayer {
	static constexpr bool passesUp = true;


	void Process(uint32, const VUB::Outbox& outbox) {
		std::array<uint8, 65> packet{};
		testPlainDeliverStatus = outbox.Deliver(std::span<const uint8>(packet.data(), packet.size()));
	}
};


using HoldStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<RecordLayer>()
		.Transform<XorTransform>()
		.Layer<HoldLayer>()
		.Link<PacketEnd>()
		.MaxPayload(4)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using PlainUpStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<PlainUpLayer>()
		.Link<PacketEnd>()
		.MaxPayload(4)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using TooLongUpStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<TooLongUpLayer>()
		.Link<PacketEnd>()
		.MaxPayload(4)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// Packet 2 comes before packets 0 and 1. It is held until the gap fills, and it goes up from Process, after packet 1. Each one passes the
// transform above HoldLayer, so the handler gets the payload (0, 1, 2), each once, and RecordLayer sees them in order.
void TestHeldPacketGoesUpFromProcess() {
	ResetTestState();
	testSeenCount = 0;
	PacketLine line(VUB::Faults{});
	HoldStack stack(line.a);
	std::array<uint8, 4> second = {0x02, 0xA7, 0xA7, 0xA7};
	std::array<uint8, 4> first = {0x00, 0xA5, 0xA5, 0xA5};
	std::array<uint8, 4> middle = {0x01, 0xA4, 0xA4, 0xA4};

	stack.FeedPacket(std::span<const uint8>(second.data(), second.size()));
	stack.FeedPacket(std::span<const uint8>(first.data(), first.size()));
	stack.Process();
	SIM_EXPECT(testReceivedCount == 1);
	SIM_EXPECT(testSeenCount == 1);
	SIM_EXPECT(testSeen[0] == 0x00);

	stack.FeedPacket(std::span<const uint8>(middle.data(), middle.size()));
	stack.Process();
	SIM_EXPECT(testReceivedCount == 3);
	SIM_EXPECT(testSeenCount == 3);
	SIM_EXPECT(testSeen[1] == 0x01);
	SIM_EXPECT(testSeen[2] == 0x02);
	SIM_EXPECT(testReceivedLength[2] == 1);
	SIM_EXPECT(testReceived[0][0] == 0x00);
	SIM_EXPECT(testReceived[1][0] == 0x01);
	SIM_EXPECT(testReceived[2][0] == 0x02);
	SIM_EXPECT(stack.Drops().transformError == 0);
	SIM_EXPECT(stack.Drops().layerError == 0);
}


// A held packet that the transform above refuses is counted by the transform, and the handler does not get it. The packets after it still go
// up in order.
void TestDeliveredPacketIsDecodedOrCounted() {
	ResetTestState();
	testSeenCount = 0;
	PacketLine line(VUB::Faults{});
	HoldStack stack(line.a);
	std::array<uint8, 4> badSecond = {0x02, 0xA7, 0xA7, 0xA8};
	std::array<uint8, 4> first = {0x00, 0xA5, 0xA5, 0xA5};
	std::array<uint8, 4> middle = {0x01, 0xA4, 0xA4, 0xA4};

	stack.FeedPacket(std::span<const uint8>(badSecond.data(), badSecond.size()));
	stack.FeedPacket(std::span<const uint8>(first.data(), first.size()));
	stack.Process();
	SIM_EXPECT(testReceivedCount == 1);

	stack.FeedPacket(std::span<const uint8>(middle.data(), middle.size()));
	stack.Process();
	SIM_EXPECT(testReceivedCount == 2);
	SIM_EXPECT(testSeenCount == 2);
	SIM_EXPECT(testSeen[1] == 0x01);
	SIM_EXPECT(stack.Drops().transformError == 1);
}


// A packet longer than the link's receive buffer is refused by Deliver and counted under layerError. The handler is not called.
void TestTooLongDeliveryIsRefused() {
	ResetTestState();
	testPlainDeliverStatus = ResultStatus::ok;
	PacketLine line(VUB::Faults{});
	TooLongUpStack stack(line.a);
	stack.Process();
	SIM_EXPECT(testPlainDeliverStatus == ResultStatus::messageTooLong);
	SIM_EXPECT(testReceivedCount == 0);
	SIM_EXPECT(stack.Drops().layerError == 1);
}


// A layer that did not declare passesUp has no path up: Deliver is refused with notSupported and the handler gets nothing.
void TestUndeclaredLayerCannotDeliver() {
	ResetTestState();
	testPlainDeliverStatus = ResultStatus::ok;
	PacketLine line(VUB::Faults{});
	PlainUpStack stack(line.a);
	stack.Process();
	SIM_EXPECT(testPlainDeliverStatus == ResultStatus::notSupported);
	SIM_EXPECT(testReceivedCount == 0);
}


int main() {
	TestHeldPacketGoesUpFromProcess();
	TestDeliveredPacketIsDecodedOrCounted();
	TestTooLongDeliveryIsRefused();
	TestUndeclaredLayerCannotDeliver();
	return SimFinish("TestVubDeliverUp");
}
