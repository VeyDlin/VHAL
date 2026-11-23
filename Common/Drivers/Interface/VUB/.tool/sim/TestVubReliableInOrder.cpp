#include "SimCheck.h"
#include "VubReliableChains.h"
#include <Drivers/Interface/VUB/Layer/VubReliable.h>
#include <Drivers/Interface/VUB/TestKit/VubLoopback.h>
#include <Drivers/Interface/VUB/TestKit/VubWire.h>
#include <Drivers/Interface/VUB/VubBytes.h>
#include <Drivers/Interface/VUB/VubMeta.h>
#include <Drivers/Interface/VUB/VubOutbox.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>

// The in-order option of the delivery layer: VubReliableOf with InOrder = true. A packet inside the window is held until the packets
// before it have gone up; it is passed up from Process through the outbox's Deliver, and it is acknowledged when it goes up. The sessions
// use the sender's numbers: the sync is number 0, the data starts at 1. The session tag of boot 0x2020 is 0x20 for counter 0. The
// payloads are two bytes, the id, little-endian.


using OrderLayer = VubReliableOf<40, 4, true>;
using PlainOrderLayer = VubReliableOf<40, 4>;
using BigSenderLayer = VubReliableOf<64, 4>;


// The packets a side sent, kept as bytes, and the payloads the side passed up, by id.
struct OrderSide {
	std::array<std::array<uint8, 64>, 16> packets{};
	std::array<std::size_t, 16> lengths{};
	std::size_t count = 0;
	std::array<uint16, 16> up{};
	std::size_t upCount = 0;
};


// The status and the count of the outcomes the sender reported, by id.
inline std::array<ResultStatus, 16> orderStatus{};
inline std::array<uint16, 16> orderOutcomes{};


// The outcomes counted for every id: ok, and any other status.
inline uint32 orderOkCount = 0;
inline uint32 orderNotOkCount = 0;


void OrderOutcome(const VubOutcome& outcome) {
	uint16 id = VubGetLe16(outcome.payload.data());
	if (id < orderStatus.size()) {
		orderStatus[id] = outcome.status;
		orderOutcomes[id]++;
	}

	if (outcome.status == ResultStatus::ok) {
		orderOkCount++;
	} else {
		orderNotOkCount++;
	}
}


void ResetOrderState() {
	orderStatus.fill(ResultStatus::busy);
	orderOutcomes.fill(0);
	orderOkCount = 0;
	orderNotOkCount = 0;
}


// The options of a sender that reports its outcomes.
PlainOrderLayer::Options OutcomeOptions() {
	PlainOrderLayer::Options options{};
	options.outcomeFn = &OrderOutcome;
	return options;
}


ResultStatus OrderSend(void* owner, std::span<const uint8> packet, const VubMeta&) {
	OrderSide* side = static_cast<OrderSide*>(owner);
	if (side->count < side->packets.size()) {
		for (std::size_t index = 0; index < packet.size(); index++) {
			side->packets[side->count][index] = packet[index];
		}

		side->lengths[side->count] = packet.size();
	}

	side->count++;
	return ResultStatus::ok;
}


ResultStatus OrderDeliver(void* owner, std::span<const uint8> packet) {
	OrderSide* side = static_cast<OrderSide*>(owner);
	if (side->upCount < side->up.size()) {
		side->up[side->upCount] = VubGetLe16(packet.data());
	}

	side->upCount++;
	return ResultStatus::ok;
}


VubOutbox OrderOutbox(OrderSide& side) {
	return VubOutbox(&side, &OrderSend, nullptr, &OrderDeliver);
}


// Adds a packet with the id as its payload, with payloadBytes bytes in all. The packet is kept in the side, as the layer returns it.
// Returns false when the layer is busy.
template<typename Layer>
bool SendOrder(Layer& layer, uint16 id, OrderSide& side, std::size_t payloadBytes = 2) {
	std::array<uint8, 64> payload{};
	VubPutLe16(payload.data(), id);
	std::array<uint8, 64> out{};
	Result<std::size_t> added = layer.Add(VubMeta{}, std::span<const uint8>(payload.data(), payloadBytes),
		std::span<uint8>(out.data(), out.size()));
	if (added.IsErr()) {
		return false;
	}

	if (side.count < side.packets.size()) {
		for (std::size_t index = 0; index < added.Value(); index++) {
			side.packets[side.count][index] = out[index];
		}

		side.lengths[side.count] = added.Value();
	}

	side.count++;
	return true;
}


// Hands the packet at index of from to the layer, and returns whether the layer took it.
template<typename Layer>
bool CarryAt(const OrderSide& from, std::size_t index, Layer& to) {
	std::span<const uint8> packet(from.packets[index].data(), from.lengths[index]);
	return to.Take(VubMeta{}, packet).IsOk();
}


// Hands every packet of from to the layer, and empties from.
template<typename Layer>
void CarryAll(OrderSide& from, Layer& to) {
	for (std::size_t index = 0; index < from.count && index < from.packets.size(); index++) {
		SIM_EXPECT(CarryAt(from, index, to));
	}

	from.count = 0;
}


// True when packet index of the side is exactly the expected bytes.
template<std::size_t Size>
bool SentIs(const OrderSide& side, std::size_t index, const std::array<uint8, Size>& expected) {
	if (index >= side.count || index >= side.packets.size() || side.lengths[index] != Size) {
		return false;
	}

	for (std::size_t byteIndex = 0; byteIndex < Size; byteIndex++) {
		if (side.packets[index][byteIndex] != expected[byteIndex]) {
			return false;
		}
	}

	return true;
}


// Starts the session of the sender with the receiver at now: the first Send is busy, the sync goes out at Process, the receiver
// acknowledges it, and the sender is established. Both sides are empty afterwards.
template<typename Sender>
void EstablishOrder(Sender& sender, OrderSide& sWire, OrderLayer& receiver, OrderSide& rWire, uint32 now) {
	SIM_EXPECT(!SendOrder(sender, 0xFFFF, sWire));
	sender.Process(now, OrderOutbox(sWire));
	SIM_EXPECT(sWire.count == 1);
	CarryAll(sWire, receiver);
	receiver.Process(now, OrderOutbox(rWire));
	CarryAll(rWire, sender);
	sender.Process(now, OrderOutbox(sWire));
	sWire.count = 0;
	rWire.count = 0;
}


// The first test: the packets of ids 0 to 2 are numbers 1 to 3. The packet of id 0 is lost, and its resends too. Ids 1 and 2 arrive and
// wait for it. The sender gives up on ids 0 to 2 at 1.4 s, and sends id 3 (number 4) at 1.45 s, with the window base 4 in its delta. The
// receiver then passes up ids 1, 2 and 3 in order, and acknowledges each of them when it goes up.
void TestHeldPacketsGoUpAfterTheSenderGivesUpTheGap() {
	ResetOrderState();
	OrderSide sWire{};
	OrderSide rWire{};
	PlainOrderLayer::Options sendOptions{};
	sendOptions.retries = 1;
	sendOptions.outcomeFn = &OrderOutcome;
	PlainOrderLayer sender(sendOptions);
	OrderLayer receiver{};
	sender.SetSession(0x2020);
	EstablishOrder(sender, sWire, receiver, rWire, 1000000);
	SIM_EXPECT(SendOrder(sender, 0, sWire));
	SIM_EXPECT(SendOrder(sender, 1, sWire));
	SIM_EXPECT(SendOrder(sender, 2, sWire));
	// Data of number 1, id 0: 01 20 01 00 00 00 00.
	std::array<uint8, 7> first = {0x01, 0x20, 0x01, 0x00, 0x00, 0x00, 0x00};
	SIM_EXPECT(SentIs(sWire, 0, first));
	SIM_EXPECT(CarryAt(sWire, 1, receiver));
	SIM_EXPECT(CarryAt(sWire, 2, receiver));
	receiver.Process(1000000, OrderOutbox(rWire));
	SIM_EXPECT(rWire.count == 0);
	SIM_EXPECT(rWire.upCount == 0);
	sWire.count = 0;

	// The resends at 1.2 s are lost too. At 1.4 s the sender gives up on all three.
	sender.Process(1200000, OrderOutbox(sWire));
	SIM_EXPECT(sWire.count == 3);
	sWire.count = 0;
	sender.Process(1400000, OrderOutbox(sWire));
	SIM_EXPECT(sWire.count == 0);
	SIM_EXPECT(orderStatus[0] == ResultStatus::retryExhausted);
	SIM_EXPECT(orderStatus[1] == ResultStatus::retryExhausted);
	SIM_EXPECT(orderStatus[2] == ResultStatus::retryExhausted);

	// Id 3 is number 4. The sender has nothing unsettled, so its base is 4 and the delta is 0: 01 20 04 00 00 03 00.
	SIM_EXPECT(SendOrder(sender, 3, sWire));
	std::array<uint8, 7> afterGiveUp = {0x01, 0x20, 0x04, 0x00, 0x00, 0x03, 0x00};
	SIM_EXPECT(SentIs(sWire, 0, afterGiveUp));
	SIM_EXPECT(CarryAt(sWire, 0, receiver));
	sWire.count = 0;
	receiver.Process(1450000, OrderOutbox(rWire));
	SIM_EXPECT(rWire.upCount == 3);
	SIM_EXPECT(rWire.up[0] == 1);
	SIM_EXPECT(rWire.up[1] == 2);
	SIM_EXPECT(rWire.up[2] == 3);
	// One acknowledgement for each packet that went up, each alone: tag 0x20, numbers 2, 3 and 4.
	SIM_EXPECT(rWire.count == 3);
	std::array<uint8, 4> ack2 = {0x12, 0x20, 0x02, 0x00};
	std::array<uint8, 4> ack3 = {0x12, 0x20, 0x03, 0x00};
	std::array<uint8, 4> ack4 = {0x12, 0x20, 0x04, 0x00};
	SIM_EXPECT(SentIs(rWire, 0, ack2));
	SIM_EXPECT(SentIs(rWire, 1, ack3));
	SIM_EXPECT(SentIs(rWire, 2, ack4));
	CarryAll(rWire, sender);
	sender.Process(1450000, OrderOutbox(sWire));
	// Id 3 is acknowledged, so it is ok. Ids 1 and 2 were given up before their acknowledgements came, so they stay retryExhausted:
	// they were passed up, which the outcome allows.
	SIM_EXPECT(orderStatus[3] == ResultStatus::ok);
	SIM_EXPECT(orderStatus[1] == ResultStatus::retryExhausted);
	SIM_EXPECT(orderOutcomes[0] == 1);
	SIM_EXPECT(orderOutcomes[1] == 1);
	SIM_EXPECT(orderOutcomes[2] == 1);
	SIM_EXPECT(orderOutcomes[3] == 1);
}


// The packets that fill the gap come up in order, each once, and each is acknowledged. A copy of a held packet is not acknowledged.
void TestGapFillReleasesInOrderAndAcknowledgesEach() {
	ResetOrderState();
	OrderSide sWire{};
	OrderSide rWire{};
	PlainOrderLayer sender(OutcomeOptions());
	OrderLayer receiver{};
	sender.SetSession(0x2020);
	EstablishOrder(sender, sWire, receiver, rWire, 1000000);
	SIM_EXPECT(SendOrder(sender, 0, sWire));
	SIM_EXPECT(SendOrder(sender, 1, sWire));
	SIM_EXPECT(SendOrder(sender, 2, sWire));
	SIM_EXPECT(CarryAt(sWire, 1, receiver));
	SIM_EXPECT(CarryAt(sWire, 2, receiver));
	receiver.Process(1000000, OrderOutbox(rWire));
	SIM_EXPECT(rWire.count == 0);
	// A copy of id 1 is held already: nothing is acknowledged and nothing goes up.
	SIM_EXPECT(CarryAt(sWire, 1, receiver));
	receiver.Process(1000000, OrderOutbox(rWire));
	SIM_EXPECT(rWire.count == 0);
	SIM_EXPECT(rWire.upCount == 0);
	// Number 1 arrives: ids 0, 1 and 2 go up in order, and are acknowledged in the same order.
	SIM_EXPECT(CarryAt(sWire, 0, receiver));
	receiver.Process(1000000, OrderOutbox(rWire));
	SIM_EXPECT(rWire.upCount == 3);
	SIM_EXPECT(rWire.up[0] == 0);
	SIM_EXPECT(rWire.up[1] == 1);
	SIM_EXPECT(rWire.up[2] == 2);
	SIM_EXPECT(rWire.count == 3);
	std::array<uint8, 4> ack1 = {0x12, 0x20, 0x01, 0x00};
	std::array<uint8, 4> ack2 = {0x12, 0x20, 0x02, 0x00};
	std::array<uint8, 4> ack3 = {0x12, 0x20, 0x03, 0x00};
	SIM_EXPECT(SentIs(rWire, 0, ack1));
	SIM_EXPECT(SentIs(rWire, 1, ack2));
	SIM_EXPECT(SentIs(rWire, 2, ack3));
	CarryAll(rWire, sender);
	sender.Process(1000000, OrderOutbox(sWire));
	SIM_EXPECT(orderStatus[0] == ResultStatus::ok);
	SIM_EXPECT(orderStatus[1] == ResultStatus::ok);
	SIM_EXPECT(orderStatus[2] == ResultStatus::ok);
	SIM_EXPECT(orderOutcomes[0] == 1);
	SIM_EXPECT(orderOutcomes[1] == 1);
	SIM_EXPECT(orderOutcomes[2] == 1);
}


// A receiver that reboots while packets are held: the held packets are gone with its session. The sender's resends of them are refused
// by the new receiver, which asks for a resync, and the sender resets them. Nothing goes up.
void TestReceiverRebootClearsHeldPackets() {
	ResetOrderState();
	OrderSide sWire{};
	OrderSide rWire{};
	OrderSide rebootWire{};
	PlainOrderLayer sender(OutcomeOptions());
	OrderLayer receiver{};
	sender.SetSession(0x2020);
	EstablishOrder(sender, sWire, receiver, rWire, 1000000);
	SIM_EXPECT(SendOrder(sender, 0, sWire));
	SIM_EXPECT(SendOrder(sender, 1, sWire));
	SIM_EXPECT(CarryAt(sWire, 1, receiver));
	receiver.Process(1000000, OrderOutbox(rWire));
	SIM_EXPECT(rWire.count == 0);
	sWire.count = 0;

	// The receiver reboots: a new layer with no session and nothing held.
	OrderLayer rebooted{};
	sender.Process(1200000, OrderOutbox(sWire));
	SIM_EXPECT(sWire.count == 2);
	SIM_EXPECT(CarryAt(sWire, 0, rebooted));
	SIM_EXPECT(CarryAt(sWire, 1, rebooted));
	rebooted.Process(1200000, OrderOutbox(rebootWire));
	// Resync request for tag 0x20.
	std::array<uint8, 2> resync = {0x08, 0x20};
	SIM_EXPECT(rebootWire.count == 1);
	SIM_EXPECT(SentIs(rebootWire, 0, resync));
	CarryAll(rebootWire, sender);
	sender.Process(1200000, OrderOutbox(sWire));
	SIM_EXPECT(rebootWire.upCount == 0);
	SIM_EXPECT(orderStatus[0] == ResultStatus::reset);
	SIM_EXPECT(orderStatus[1] == ResultStatus::reset);
	SIM_EXPECT(orderOutcomes[0] == 1);
	SIM_EXPECT(orderOutcomes[1] == 1);
}


// A packet longer than the receiver's PayloadCapacity is not held and not acknowledged: the sender sends it again, and it is given up.
void TestPayloadLargerThanCapacityIsNotHeld() {
	ResetOrderState();
	OrderSide sWire{};
	OrderSide rWire{};
	BigSenderLayer::Options bigOptions{};
	bigOptions.outcomeFn = &OrderOutcome;
	BigSenderLayer sender(bigOptions);
	OrderLayer receiver{};
	sender.SetSession(0x2020);
	EstablishOrder(sender, sWire, receiver, rWire, 1000000);
	SIM_EXPECT(SendOrder(sender, 7, sWire, 50));
	// The data header is 5 bytes (flags, tag, number, delta), so the packet is 5 + 50 bytes.
	SIM_EXPECT(sWire.lengths[0] == 5 + 50);
	SIM_EXPECT(CarryAt(sWire, 0, receiver));
	receiver.Process(1000000, OrderOutbox(rWire));
	SIM_EXPECT(rWire.count == 0);
	SIM_EXPECT(rWire.upCount == 0);
}


// Packets on the faulty packet line, with drops, duplicates, corrupted bits and delays that reorder them. The receiver passes up the
// packets in the order of their numbers, each at most once. An ok outcome means the packet was passed up.
inline std::array<uint16, 512> lineOrder{};
inline std::size_t lineOrderCount = 0;


void LineOrderReceive(const VubMeta& meta, std::span<const uint8> payload) {
	ReliableReceive(meta, payload);
	if (lineOrderCount < lineOrder.size()) {
		lineOrder[lineOrderCount] = ReliableIdOf(payload);
	}

	lineOrderCount++;
}


using InOrderPacket = Vub<+[] {
	return VubBuilder()
		.Layer<VubReliableOf<40, 4, true>>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40).OnOutcome<&ReliableOutcome>().OnRestart<&ReliableRestart>()
		.Transform<XorTransform>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&LineOrderReceive>()
		.Clock<&TestClock>();
}>;


// Sends total packets, one attempt per step, and runs both stacks each step over the line. Stops when every outcome is in or after the
// time limit. The same loop as the faults test.
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


void TestFaultyLineComesUpInOrderEachOnce() {
	ResetReliableState();
	lineOrder.fill(0);
	lineOrderCount = 0;
	VubFaults faults;
	faults.seed = 0x2B2B;
	faults.dropPercent = 20;
	faults.corruptPercent = 10;
	faults.duplicatePercent = 10;
	faults.maxDelayUs = 500;
	ReportingPacketLine line(faults);
	InOrderPacket near(line.a);
	near.Find<OrderLayer>().SetSession(0x1234);
	InOrderPacket far(line.b);
	far.Find<OrderLayer>().SetSession(0x1234);
	near.Process();
	far.Process();
	const std::size_t total = 150;
	RunFaultyTraffic(line, near, far, total, 100, 120000000);
	std::size_t okCount = 0;
	for (std::size_t id = 0; id < total; id++) {
		SIM_EXPECT(testOutcomes[id] == 1);
		SIM_EXPECT(testDelivered[id] <= 1);
		if (testStatus[id] == ResultStatus::ok) {
			SIM_EXPECT(testDelivered[id] == 1);
			okCount++;
		} else {
			SIM_EXPECT(testStatus[id] == ResultStatus::retryExhausted);
		}
	}

	SIM_EXPECT(okCount > total / 2);
	SIM_EXPECT(lineOrderCount > 0);
	SIM_EXPECT(lineOrderCount <= lineOrder.size());
	for (std::size_t index = 1; index < lineOrderCount && index < lineOrder.size(); index++) {
		SIM_EXPECT(lineOrder[index] > lineOrder[index - 1]);
	}

	SIM_EXPECT(line.toB.Counts().dropped + line.toA.Counts().dropped > 0);
	SIM_EXPECT(testRestarts == 0);
}


// Eight-bit numbers over 300 packets with no gap: the receiver's base wraps several times. Each packet goes up once, in turn, and is
// reported ok. The base of the receiver must not move backward when the sender's base has not moved for a long time.
void TestEightBitNumbersWrapInOrder() {
	ResetOrderState();
	OrderSide sWire{};
	OrderSide rWire{};
	PlainOrderLayer::Options sendOptions = OutcomeOptions();
	sendOptions.sequenceBits = 8;
	PlainOrderLayer sender(sendOptions);
	OrderLayer::Options receiveOptions{};
	receiveOptions.sequenceBits = 8;
	OrderLayer receiver(receiveOptions);
	sender.SetSession(0x2020);
	EstablishOrder(sender, sWire, receiver, rWire, 1000000);
	uint32 now = 1000000;
	for (uint16 id = 0; id < 300; id++) {
		now += 1000;
		SIM_EXPECT(SendOrder(sender, id, sWire));
		CarryAll(sWire, receiver);
		receiver.Process(now, OrderOutbox(rWire));
		CarryAll(rWire, sender);
		sender.Process(now, OrderOutbox(sWire));
		sWire.count = 0;
		rWire.count = 0;
	}

	SIM_EXPECT(rWire.upCount == 300);
	SIM_EXPECT(orderOkCount == 300);
	SIM_EXPECT(orderNotOkCount == 0);
}


int main() {
	TestEightBitNumbersWrapInOrder();
	TestHeldPacketsGoUpAfterTheSenderGivesUpTheGap();
	TestGapFillReleasesInOrderAndAcknowledgesEach();
	TestReceiverRebootClearsHeldPackets();
	TestPayloadLargerThanCapacityIsNotHeld();
	TestFaultyLineComesUpInOrderEachOnce();
	return SimFinish("TestVubReliableInOrder");
}
