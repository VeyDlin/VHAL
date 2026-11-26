#include "SimCheck.h"
#include "ReliableChains.h"
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Drivers/Interface/VUB/TestKit/Wire.h>
#include <Drivers/Interface/VUB/Bytes.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Outbox.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>

// The in-order option of the delivery layer: VUB::ReliableOf with InOrder = true. A packet inside the window is held until the packets
// before it have gone up; it is passed up from Process through the outbox's Deliver, and it is acknowledged when it goes up. The sessions
// use the sender's numbers: the sync has the first number of its boot (0xA6E0 for boot 0x2020), and the data starts after it. The session
// tag of boot 0x2020 is 0x46 for counter 0. The payloads are two bytes, the id, little-endian.


using OrderLayer = VUB::ReliableOf<40, 4, true>;


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


void OrderOutcome(const VUB::Outcome& outcome) {
	uint16 id = VUB::GetLe16(outcome.payload.data());
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


// The options of the senders and receivers of this file. A layer takes its options as its template argument, so each set is a constant
// of its own, and the sender types below are the layers built from them.
constexpr VUB::ReliableOptions<40, 4> OutcomeOptions{.outcomeFn = &VUB::OutcomeHandler<&OrderOutcome>};
constexpr VUB::ReliableOptions<40, 4> RetryOnceOptions{.retries = 1, .outcomeFn = &VUB::OutcomeHandler<&OrderOutcome>};
constexpr VUB::ReliableOptions<40, 4> EightBitOutcomeOptions{.sequenceBits = 8, .outcomeFn = &VUB::OutcomeHandler<&OrderOutcome>};
constexpr VUB::ReliableOptions<40, 4> EightBitOptions{.sequenceBits = 8};
constexpr VUB::ReliableOptions<64, 4> BigOutcomeOptions{.outcomeFn = &VUB::OutcomeHandler<&OrderOutcome>};
using OutcomeSender = VUB::ReliableOf<40, 4, false, OutcomeOptions>;
using RetryOnceSender = VUB::ReliableOf<40, 4, false, RetryOnceOptions>;
using EightBitSender = VUB::ReliableOf<40, 4, false, EightBitOutcomeOptions>;
using EightBitReceiver = VUB::ReliableOf<40, 4, true, EightBitOptions>;
using BigSenderLayer = VUB::ReliableOf<64, 4, false, BigOutcomeOptions>;


ResultStatus OrderSend(void* owner, std::span<const uint8> packet, const VUB::Meta&) {
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
		side->up[side->upCount] = VUB::GetLe16(packet.data());
	}

	side->upCount++;
	return ResultStatus::ok;
}


VUB::Outbox OrderOutbox(OrderSide& side) {
	return VUB::Outbox(&side, &OrderSend, nullptr, &OrderDeliver);
}


// Adds a packet with the id as its payload, with payloadBytes bytes in all. The packet is kept in the side, as the layer returns it.
// Returns false when the layer is busy.
template<typename Layer>
bool SendOrder(Layer& layer, uint16 id, OrderSide& side, std::size_t payloadBytes = 2) {
	std::array<uint8, 64> payload{};
	VUB::PutLe16(payload.data(), id);
	std::array<uint8, 64> out{};
	Result<std::size_t> added = layer.Add(VUB::Meta{}, std::span<const uint8>(payload.data(), payloadBytes),
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
	return to.Take(VUB::Meta{}, packet).IsOk();
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
template<typename Sender, typename Receiver>
void EstablishOrder(Sender& sender, OrderSide& sWire, Receiver& receiver, OrderSide& rWire, uint32 now) {
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


// The packets of ids 0 to 2 are numbers 0xA6E1 to 0xA6E3 (the first number of boot 0x2020 is 0xA6E0). The packet of id 0 is lost, and its
// resends too. Ids 1 and 2 arrive and wait for it. The sender gives up on ids 0 to 2 at 1.4 s, and sends id 3 (number 0xA6E4) at 1.45 s,
// with its window base 0xA6E4 in the delta. The base of the receiver moves to that number, so ids 1 and 2, held below it, are dropped: the
// sender has given them up, so they are not acknowledged and do not go up. Only id 3 goes up, and only its acknowledgement is sent.
void TestGivenUpGapDropsTheHeldPacketsBelowTheSenderBase() {
	ResetOrderState();
	OrderSide sWire{};
	OrderSide rWire{};
	RetryOnceSender sender;
	OrderLayer receiver{};
	sender.SetSession(0x2020);
	EstablishOrder(sender, sWire, receiver, rWire, 1000000);
	SIM_EXPECT(SendOrder(sender, 0, sWire));
	SIM_EXPECT(SendOrder(sender, 1, sWire));
	SIM_EXPECT(SendOrder(sender, 2, sWire));
	// Data of number 0xA6E1, id 0: 01 46 E1 A6 00 00 00.
std::array<uint8, 7> first = {0x01, 0x46, 0xE1, 0xA6, 0x00, 0x00, 0x00};
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

	// Id 3 is number 0xA6E4. The sender has nothing unsettled, so its base is 0xA6E4 and the delta is 0: 01 46 E4 A6 00 03 00.
	SIM_EXPECT(SendOrder(sender, 3, sWire));
	std::array<uint8, 7> afterGiveUp = {0x01, 0x46, 0xE4, 0xA6, 0x00, 0x03, 0x00};
	SIM_EXPECT(SentIs(sWire, 0, afterGiveUp));
	SIM_EXPECT(CarryAt(sWire, 0, receiver));
	sWire.count = 0;
	receiver.Process(1450000, OrderOutbox(rWire));
	SIM_EXPECT(rWire.upCount == 1);
	SIM_EXPECT(rWire.up[0] == 3);
	// One acknowledgement, for the packet that went up: tag 0x46, number 0xA6E4.
	SIM_EXPECT(rWire.count == 1);
	std::array<uint8, 4> ack4 = {0x12, 0x46, 0xE4, 0xA6};
	SIM_EXPECT(SentIs(rWire, 0, ack4));
	CarryAll(rWire, sender);
	sender.Process(1450000, OrderOutbox(sWire));
	// Id 3 is acknowledged, so it is ok. Ids 0, 1 and 2 were given up, so they stay retryExhausted and are not passed up.
	SIM_EXPECT(orderStatus[3] == ResultStatus::ok);
	SIM_EXPECT(orderStatus[1] == ResultStatus::retryExhausted);
	SIM_EXPECT(orderStatus[2] == ResultStatus::retryExhausted);
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
	OutcomeSender sender;
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
	std::array<uint8, 4> ack1 = {0x12, 0x46, 0xE1, 0xA6};
	std::array<uint8, 4> ack2 = {0x12, 0x46, 0xE2, 0xA6};
	std::array<uint8, 4> ack3 = {0x12, 0x46, 0xE3, 0xA6};
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
	OutcomeSender sender;
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
	// Resync request for tag 0x46.
	std::array<uint8, 2> resync = {0x08, 0x46};
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
	BigSenderLayer sender;
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


void LineOrderReceive(const VUB::Meta& meta, std::span<const uint8> payload) {
	ReliableReceive(meta, payload);
	if (lineOrderCount < lineOrder.size()) {
		lineOrder[lineOrderCount] = ReliableIdOf(payload);
	}

	lineOrderCount++;
}


using InOrderPacket = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::ReliableOf<40, 4, true>>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40).OnOutcome<&ReliableOutcome>().OnRestart<&ReliableRestart>()
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
	VUB::Faults faults;
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
	EightBitSender sender;
	EightBitReceiver receiver;
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


// Hands raw packet bytes to the layer, and returns whether the layer took them.
template<typename Layer, std::size_t Size>
bool TakeRaw(Layer& to, const std::array<uint8, Size>& packet) {
	return to.Take(VUB::Meta{}, std::span<const uint8>(packet.data(), packet.size())).IsOk();
}


// A held packet of a session that the peer has left must not go up when the peer's session is established: the sync's acknowledgement
// establishes the session only after the held packets have gone up. The peer P (boot 0x1234, number 0x012C for its sync) gets the sync of
// boot 0x5656 (number 0xA07A), a packet of its old session (number 0xA07B, id 7, held) and the acknowledgement of P's sync, all in one
// drain. Until P's Process has passed the held packet up, P's Send is busy, so no packet of the new session goes out before it.
void TestSyncAckWaitsForTheHeldPacketsOfTheOldSession() {
	ResetOrderState();
	OrderSide pWire{};
	OrderLayer peer{};
	peer.SetSession(0x1234);
	SIM_EXPECT(!SendOrder(peer, 0xFFFF, pWire));
	peer.Process(1000000, OrderOutbox(pWire));
	// The sync of P: tag 0x65, number 0x012C, boot 0x1234, counter 0.
	std::array<uint8, 8> pSync = {0x05, 0x65, 0x2C, 0x01, 0x00, 0x34, 0x12, 0x00};
	SIM_EXPECT(SentIs(pWire, 0, pSync));
	pWire.count = 0;

	// The sync of D (boot 0x5656, counter 0, tag 0x1E, number 0xA07A); the held packet of D (number 0xA07B, delta 0, id 7), with the tag 0x83
	// that is bound to P's boot value (0x1E + 0x65); and D's acknowledgement of P's sync (tag 0x65, number 0x012C, D's boot value 0x5656).
	std::array<uint8, 8> dSync = {0x05, 0x1E, 0x7A, 0xA0, 0x00, 0x56, 0x56, 0x00};
	std::array<uint8, 7> dHeld = {0x01, 0x83, 0x7B, 0xA0, 0x00, 0x07, 0x00};
	std::array<uint8, 6> dAck = {0x12, 0x65, 0x2C, 0x01, 0x56, 0x56};
	SIM_EXPECT(TakeRaw(peer, dSync));
	SIM_EXPECT(TakeRaw(peer, dHeld));
	SIM_EXPECT(TakeRaw(peer, dAck));
	SIM_EXPECT(!SendOrder(peer, 0x0001, pWire));
	peer.Process(1000000, OrderOutbox(pWire));

	// The held packet went up first. Its acknowledgement and the one of D's sync are sent in the order they were owed.
	SIM_EXPECT(pWire.upCount == 1);
	SIM_EXPECT(pWire.up[0] == 7);
	std::array<uint8, 6> ackSync = {0x12, 0x1E, 0x7A, 0xA0, 0x34, 0x12};
	std::array<uint8, 4> ackHeld = {0x12, 0x83, 0x7B, 0xA0};
	SIM_EXPECT(pWire.count == 2);
	SIM_EXPECT(SentIs(pWire, 0, ackSync));
	SIM_EXPECT(SentIs(pWire, 1, ackHeld));

	// The session is established: the first packet of P's session has number 0x012D, delta 0, tag 0x83 (bound to D's boot value 0x5656),
	// and goes out with the id 1.
	SIM_EXPECT(SendOrder(peer, 0x0001, pWire));
	std::array<uint8, 7> ping = {0x01, 0x83, 0x2D, 0x01, 0x00, 0x01, 0x00};
	SIM_EXPECT(SentIs(pWire, 2, ping));
}


// A data packet queued in the drain of the sync's acknowledgement is busy until the Process after it; the packet then goes out with the
// number after the sync.
void TestDataQueuedInTheSyncAckDrainWaitsForTheProcess() {
	ResetOrderState();
	OrderSide wire{};
	OrderLayer sender{};
	sender.SetSession(0x1234);
	SIM_EXPECT(!SendOrder(sender, 0xFFFF, wire));
	sender.Process(1000000, OrderOutbox(wire));
	wire.count = 0;
	std::array<uint8, 6> ack = {0x12, 0x65, 0x2C, 0x01, 0x00, 0x00};
	SIM_EXPECT(TakeRaw(sender, ack));
	SIM_EXPECT(!SendOrder(sender, 0x0007, wire));
	sender.Process(1000000, OrderOutbox(wire));
	SIM_EXPECT(wire.count == 0);
	SIM_EXPECT(SendOrder(sender, 0x0007, wire));
	// Number 0x012D, delta 0, id 7.
	std::array<uint8, 7> data = {0x01, 0x65, 0x2D, 0x01, 0x00, 0x07, 0x00};
	SIM_EXPECT(SentIs(wire, 0, data));
}


// The packet that is due to go up in order is held by the receiver with the packets above it, and its sender has given up on the gap below
// it. The held packets above the sender's base go up in order, once the sender's base has moved past the gap.
void TestHeldPacketsAboveTheSenderBaseGoUpAfterAGiveUp() {
	ResetOrderState();
	OrderSide sWire{};
	OrderSide rWire{};
	RetryOnceSender sender;
	OrderLayer receiver{};
	sender.SetSession(0x1234);
	EstablishOrder(sender, sWire, receiver, rWire, 1000000);

	// Id 0 (number 0x012D) is lost, and so is its resend at 1.2 s. Ids 1 and 2 (numbers 0x012E and 0x012F) are sent at 1.1 s and arrive.
	SIM_EXPECT(SendOrder(sender, 0, sWire));
	SIM_EXPECT(SentIs(sWire, 0, std::array<uint8, 7>{0x01, 0x65, 0x2D, 0x01, 0x00, 0x00, 0x00}));
	sWire.count = 0;
	sender.Process(1100000, OrderOutbox(sWire));
	SIM_EXPECT(SendOrder(sender, 1, sWire));
	SIM_EXPECT(SendOrder(sender, 2, sWire));
	SIM_EXPECT(SentIs(sWire, 0, std::array<uint8, 7>{0x01, 0x65, 0x2E, 0x01, 0x01, 0x01, 0x00}));
	SIM_EXPECT(SentIs(sWire, 1, std::array<uint8, 7>{0x01, 0x65, 0x2F, 0x01, 0x02, 0x02, 0x00}));
	SIM_EXPECT(CarryAt(sWire, 0, receiver));
	SIM_EXPECT(CarryAt(sWire, 1, receiver));
	sWire.count = 0;
	receiver.Process(1100000, OrderOutbox(rWire));
	SIM_EXPECT(rWire.count == 0);
	SIM_EXPECT(rWire.upCount == 0);

	// The resend of id 0 at 1.2 s is lost. Ids 1 and 2 are resent at 1.3 s: they are copies of held packets, and are not acknowledged.
	sender.Process(1200000, OrderOutbox(sWire));
	SIM_EXPECT(sWire.count == 1);
	sWire.count = 0;
	sender.Process(1300000, OrderOutbox(sWire));
	SIM_EXPECT(sWire.count == 2);
	SIM_EXPECT(CarryAt(sWire, 0, receiver));
	SIM_EXPECT(CarryAt(sWire, 1, receiver));
	sWire.count = 0;
	receiver.Process(1300000, OrderOutbox(rWire));
	SIM_EXPECT(rWire.count == 0);

	// At 1.4 s the sender gives up on id 0. Its base is then number 0x012E (id 1), and id 3 (number 0x0130) at 1.45 s has delta 2.
	sender.Process(1400000, OrderOutbox(sWire));
	SIM_EXPECT(orderStatus[0] == ResultStatus::retryExhausted);
	SIM_EXPECT(sWire.count == 0);
	sender.Process(1450000, OrderOutbox(sWire));
	SIM_EXPECT(SendOrder(sender, 3, sWire));
	SIM_EXPECT(SentIs(sWire, 0, std::array<uint8, 7>{0x01, 0x65, 0x30, 0x01, 0x02, 0x03, 0x00}));
	SIM_EXPECT(CarryAt(sWire, 0, receiver));
	sWire.count = 0;

	// The base moves to number 0x012E: ids 1, 2 and 3 go up in order, and each is acknowledged when it goes up.
	receiver.Process(1450000, OrderOutbox(rWire));
	SIM_EXPECT(rWire.upCount == 3);
	SIM_EXPECT(rWire.up[0] == 1);
	SIM_EXPECT(rWire.up[1] == 2);
	SIM_EXPECT(rWire.up[2] == 3);
	SIM_EXPECT(rWire.count == 3);
	SIM_EXPECT(SentIs(rWire, 0, std::array<uint8, 4>{0x12, 0x65, 0x2E, 0x01}));
	SIM_EXPECT(SentIs(rWire, 1, std::array<uint8, 4>{0x12, 0x65, 0x2F, 0x01}));
	SIM_EXPECT(SentIs(rWire, 2, std::array<uint8, 4>{0x12, 0x65, 0x30, 0x01}));
	CarryAll(rWire, sender);
	sender.Process(1450000, OrderOutbox(sWire));
	SIM_EXPECT(orderStatus[1] == ResultStatus::ok);
	SIM_EXPECT(orderStatus[2] == ResultStatus::ok);
	SIM_EXPECT(orderStatus[3] == ResultStatus::ok);
	SIM_EXPECT(orderStatus[0] == ResultStatus::retryExhausted);
}


// A held packet below the sender's base is dropped when the base moves past it: the sender has given up on it, so it is not acknowledged
// and it does not go up. Number 0x0130 (id 3) is lost and given up, number 0x0131 (id 4) is held behind it; the sender gives up on id 4 too,
// and its next packet, 12 s later, carries the base 0x0132. Id 4 does not go up then: only id 5 does.
void TestHeldPacketBelowTheSenderBaseIsNotPassedUp() {
	ResetOrderState();
	OrderSide sWire{};
	OrderSide rWire{};
	RetryOnceSender sender;
	OrderLayer receiver{};
	sender.SetSession(0x1234);
	EstablishOrder(sender, sWire, receiver, rWire, 1000000);

	// Ids 0 to 2 (numbers 0x012D to 0x012F) go up and are acknowledged.
	for (uint16 id = 0; id < 3; id++) {
		SIM_EXPECT(SendOrder(sender, id, sWire));
	}

	for (std::size_t index = 0; index < 3; index++) {
		SIM_EXPECT(CarryAt(sWire, index, receiver));
	}

	receiver.Process(1000000, OrderOutbox(rWire));
	SIM_EXPECT(rWire.upCount == 3);
	CarryAll(rWire, sender);
	sender.Process(1000000, OrderOutbox(sWire));
	sWire.count = 0;

	// Id 3 (number 0x0130) is lost. Id 4 (number 0x0131) arrives and is held behind it.
	SIM_EXPECT(SendOrder(sender, 3, sWire));
	sWire.count = 0;
	SIM_EXPECT(SendOrder(sender, 4, sWire));
	SIM_EXPECT(CarryAt(sWire, 0, receiver));
	sWire.count = 0;
	receiver.Process(1000000, OrderOutbox(rWire));
	SIM_EXPECT(rWire.count == 0);

	// The resends of ids 3 and 4 at 1.2 s are lost, and at 1.4 s the sender gives up on both.
	sender.Process(1200000, OrderOutbox(sWire));
	SIM_EXPECT(sWire.count == 2);
	sWire.count = 0;
	sender.Process(1400000, OrderOutbox(sWire));
	SIM_EXPECT(orderStatus[3] == ResultStatus::retryExhausted);
	SIM_EXPECT(orderStatus[4] == ResultStatus::retryExhausted);

	// Id 5 is number 0x0132, the sender's base, with delta 0. It arrives 12 s later.
	sender.Process(13400000, OrderOutbox(sWire));
	SIM_EXPECT(SendOrder(sender, 5, sWire));
	SIM_EXPECT(SentIs(sWire, 0, std::array<uint8, 7>{0x01, 0x65, 0x32, 0x01, 0x00, 0x05, 0x00}));
	SIM_EXPECT(CarryAt(sWire, 0, receiver));
	sWire.count = 0;
	receiver.Process(13400000, OrderOutbox(rWire));
	SIM_EXPECT(rWire.upCount == 4);
	SIM_EXPECT(rWire.up[3] == 5);
	SIM_EXPECT(rWire.count == 1);
	SIM_EXPECT(SentIs(rWire, 0, std::array<uint8, 4>{0x12, 0x65, 0x32, 0x01}));
}


int main() {
	TestEightBitNumbersWrapInOrder();
	TestGivenUpGapDropsTheHeldPacketsBelowTheSenderBase();
	TestGapFillReleasesInOrderAndAcknowledgesEach();
	TestReceiverRebootClearsHeldPackets();
	TestPayloadLargerThanCapacityIsNotHeld();
	TestFaultyLineComesUpInOrderEachOnce();
	TestSyncAckWaitsForTheHeldPacketsOfTheOldSession();
	TestDataQueuedInTheSyncAckDrainWaitsForTheProcess();
	TestHeldPacketsAboveTheSenderBaseGoUpAfterAGiveUp();
	TestHeldPacketBelowTheSenderBaseIsNotPassedUp();
	return SimFinish("TestVubReliableInOrder");
}
