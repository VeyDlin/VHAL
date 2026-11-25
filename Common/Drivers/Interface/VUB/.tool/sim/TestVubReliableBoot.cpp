#include "SimCheck.h"
#include <Drivers/Interface/VUB/Blocks.h>
#include <Drivers/Interface/VUB/Bytes.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Outbox.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>

// The first number of a session is derived from its boot value, so two sessions that share a tag (boot n with counter 1 and boot n + 1
// with counter 0) still have different sync numbers. The numbers below are written by hand from the rule: the first number of boot b is
// b times 0x9E37, in 16 bits (its low 8 bits with SequenceBits(8)). The boot values used here are 0x1233, 0x1234, 0x1235 and 0x10F2, with
// the first numbers 0x62F5, 0x012C, 0x9F63 and 0xFFFE. The tag is the counter plus the low byte of the boot value.


using BootLayer = VUB::ReliableOf<40, 4>;
using BootOrderLayer = VUB::ReliableOf<40, 4, true>;

// The layers with 8-bit numbers, without and with the in-order option.
constexpr VUB::ReliableOptions<40, 4> EightBitOptions{.sequenceBits = 8};
using EightBitLayer = VUB::ReliableOf<40, 4, false, EightBitOptions>;
using EightBitOrderLayer = VUB::ReliableOf<40, 4, true, EightBitOptions>;


// The packets a side sent, kept as bytes, and the ids it passed up, in order.
struct BootSide {
	std::array<std::array<uint8, 64>, 16> packets{};
	std::array<std::size_t, 16> lengths{};
	std::size_t count = 0;
	std::array<uint16, 16> up{};
	std::size_t upCount = 0;
};


ResultStatus BootSend(void* owner, std::span<const uint8> packet, const VUB::Meta&) {
	BootSide* side = static_cast<BootSide*>(owner);
	if (side->count < side->packets.size()) {
		for (std::size_t index = 0; index < packet.size(); index++) {
			side->packets[side->count][index] = packet[index];
		}

		side->lengths[side->count] = packet.size();
	}

	side->count++;
	return ResultStatus::ok;
}


ResultStatus BootDeliver(void* owner, std::span<const uint8> packet) {
	BootSide* side = static_cast<BootSide*>(owner);
	if (side->upCount < side->up.size()) {
		side->up[side->upCount] = VUB::GetLe16(packet.data());
	}

	side->upCount++;
	return ResultStatus::ok;
}


VUB::Outbox BootOutbox(BootSide& side) {
	return VUB::Outbox(&side, &BootSend, nullptr, &BootDeliver);
}


// Hands the packet at index of from to the layer. A packet the layer passes up at once is recorded in receiving, as the application gets it.
template<typename Layer>
bool BootCarry(const BootSide& from, std::size_t index, Layer& to, BootSide& receiving) {
	std::span<const uint8> packet(from.packets[index].data(), from.lengths[index]);
	Result<VUB::Taken> taken = to.Take(VUB::Meta{}, packet);
	if (taken.IsErr()) {
		return false;
	}

	if (taken.Value().deliver) {
		std::span<const uint8> payload = packet.subspan(taken.Value().consumed);
		if (receiving.upCount < receiving.up.size()) {
			receiving.up[receiving.upCount] = VUB::GetLe16(payload.data());
		}

		receiving.upCount++;
	}

	return true;
}


// Hands raw packet bytes to the layer, and returns whether the layer took them.
template<typename Layer>
bool BootTake(Layer& to, std::span<const uint8> packet) {
	return to.Take(VUB::Meta{}, packet).IsOk();
}


// True when packet index of the side is exactly the expected bytes.
template<std::size_t Size>
bool BootSentIs(const BootSide& side, std::size_t index, const std::array<uint8, Size>& expected) {
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


// Adds a payload of two bytes, the id, to the layer, and keeps the packet in the side. Returns false when the layer is busy.
template<typename Layer>
bool BootAdd(Layer& layer, uint16 id, BootSide& side) {
	std::array<uint8, 2> payload{};
	VUB::PutLe16(payload.data(), id);
	std::array<uint8, 64> out{};
	Result<std::size_t> added = layer.Add(VUB::Meta{}, std::span<const uint8>(payload.data(), payload.size()),
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


// True when the layer is busy for a Send of two bytes, which does not put a packet into the side.
template<typename Layer>
bool BootIsBusy(Layer& layer) {
	BootSide scratch{};
	return !BootAdd(layer, 0x0000, scratch);
}


// The first Send of a session: it is busy, and its sync goes out at the next Process.
template<typename Layer>
void BootStartSession(Layer& sender, BootSide& wire, uint32 nowUs) {
	SIM_EXPECT(BootIsBusy(sender));
	sender.Process(nowUs, BootOutbox(wire));
}


// Gives a packet of the side to the layer as a packet of raw bytes: the side's packet at index.
template<typename Layer>
bool BootTakeAt(Layer& to, const BootSide& from, std::size_t index) {
	return BootTake(to, std::span<const uint8>(from.packets[index].data(), from.lengths[index]));
}


// The sync of boot 0x1234 with counter 0, as the sender writes it: flags 0x05, tag 0x34, number 0x012C (LE 2C 01), delta 0x00, boot value
// 0x1234 (LE 34 12), counter 0x00.
void TestSyncNumberIsTheFirstNumberOfTheBoot() {
	BootSide wire{};
	BootLayer sender;
	sender.SetSession(0x1234);
	BootStartSession(sender, wire, 1000000);
	SIM_EXPECT(wire.count == 1);
	std::array<uint8, 8> sync = {0x05, 0x34, 0x2C, 0x01, 0x00, 0x34, 0x12, 0x00};
	SIM_EXPECT(BootSentIs(wire, 0, sync));
}


// A late acknowledgement of the sync of an older boot. Boot 0x1233 with counter 1 has the tag 0x34 and the first number 0x62F5; the boot
// 0x1234 that follows has counter 0, the same tag 0x34, and the first number 0x012C. The late acknowledgement of the older sync carries
// the new session's tag, but its number is the older sync's, so it does not establish the new session. An acknowledgement numbered 0 is
// not the acknowledgement of this sync either. Only the acknowledgement numbered 0x012C does.
void TestLateAckOfOlderBootSyncDoesNotEstablishTheNewSession() {
	BootSide wire{};
	BootLayer sender;
	sender.SetSession(0x1234);
	BootStartSession(sender, wire, 1000000);
	SIM_EXPECT(wire.count == 1);
	wire.count = 0;
	std::array<uint8, 2> payload = {0x01, 0x00};
	std::array<uint8, 64> out{};

	// The acknowledgement of the older sync: tag 0x34, number 0x62F5 (LE F5 62).
	std::array<uint8, 4> olderAck = {0x12, 0x34, 0xF5, 0x62};
	SIM_EXPECT(BootTake(sender, std::span<const uint8>(olderAck.data(), olderAck.size())));
	sender.Process(1000000, BootOutbox(wire));
	SIM_EXPECT(sender.Add(VUB::Meta{}, std::span<const uint8>(payload.data(), payload.size()),
		std::span<uint8>(out.data(), out.size())).IsErr());

	// The acknowledgement numbered 0: not the number of this sync.
	std::array<uint8, 4> zeroAck = {0x12, 0x34, 0x00, 0x00};
	SIM_EXPECT(BootTake(sender, std::span<const uint8>(zeroAck.data(), zeroAck.size())));
	sender.Process(1000000, BootOutbox(wire));
	SIM_EXPECT(sender.Add(VUB::Meta{}, std::span<const uint8>(payload.data(), payload.size()),
		std::span<uint8>(out.data(), out.size())).IsErr());

	// The acknowledgement of this sync: tag 0x34, number 0x012C (LE 2C 01). The session is established.
	std::array<uint8, 4> ack = {0x12, 0x34, 0x2C, 0x01};
	SIM_EXPECT(BootTake(sender, std::span<const uint8>(ack.data(), ack.size())));
	sender.Process(1000000, BootOutbox(wire));
	SIM_EXPECT(sender.Add(VUB::Meta{}, std::span<const uint8>(payload.data(), payload.size()),
		std::span<uint8>(out.data(), out.size())).IsOk());
}


// Three boots in a row, each with its own sender. Boot 0x1233 reaches counter 1: a resync request for its tag 0x33 ends its first session,
// and the next sync has counter 1 (tag 0x34, number 0x62F5). Boot 0x1234 starts with counter 0 (tag 0x34, number 0x012C), and the late
// acknowledgement of boot 0x1233's counter-1 sync reaches it: it must not establish. Boot 0x1235 (tag 0x35, number 0x9F63) gets the late
// acknowledgement of boot 0x1234's sync, which has another tag, and it must not establish either. Each boot is established by its own
// acknowledgement, and its first data packet has the number after its sync.
void TestBootCounterWithThreeBoots() {
	BootSide wire{};
	std::array<uint8, 2> payload = {0x02, 0x00};
	std::array<uint8, 64> out{};

	// Boot 0x1233, counter 0: the sync of tag 0x33 and number 0x62F5 is acknowledged, and the session is established.
	BootLayer first;
	first.SetSession(0x1233);
	BootStartSession(first, wire, 1000000);
	std::array<uint8, 8> firstSync = {0x05, 0x33, 0xF5, 0x62, 0x00, 0x33, 0x12, 0x00};
	SIM_EXPECT(BootSentIs(wire, 0, firstSync));
	wire.count = 0;
	std::array<uint8, 4> firstAck = {0x12, 0x33, 0xF5, 0x62};
	SIM_EXPECT(BootTake(first, std::span<const uint8>(firstAck.data(), firstAck.size())));
	first.Process(1000000, BootOutbox(wire));

	// A resync request for tag 0x33 ends the session. The next Send starts a session whose sync has counter 1: tag 0x34, number 0x62F5.
	std::array<uint8, 2> resync = {0x08, 0x33};
	SIM_EXPECT(BootTake(first, std::span<const uint8>(resync.data(), resync.size())));
	first.Process(1000000, BootOutbox(wire));
	BootStartSession(first, wire, 1000000);
	std::array<uint8, 8> counterOneSync = {0x05, 0x34, 0xF5, 0x62, 0x00, 0x33, 0x12, 0x01};
	SIM_EXPECT(BootSentIs(wire, 0, counterOneSync));
	wire.count = 0;
	std::array<uint8, 4> counterOneAck = {0x12, 0x34, 0xF5, 0x62};
	SIM_EXPECT(BootTake(first, std::span<const uint8>(counterOneAck.data(), counterOneAck.size())));
	first.Process(1000000, BootOutbox(wire));
	SIM_EXPECT(BootAdd(first, 0x0001, wire));

	// Boot 0x1234, counter 0: tag 0x34, number 0x012C. The late acknowledgement of boot 0x1233's counter-1 sync (tag 0x34, number 0x62F5)
	// arrives while this sync waits. It does not establish the session, and the Send stays busy.
	BootLayer second;
	second.SetSession(0x1234);
	BootSide secondWire{};
	BootStartSession(second, secondWire, 2000000);
	std::array<uint8, 8> secondSync = {0x05, 0x34, 0x2C, 0x01, 0x00, 0x34, 0x12, 0x00};
	SIM_EXPECT(BootSentIs(secondWire, 0, secondSync));
	secondWire.count = 0;
	SIM_EXPECT(BootTake(second, std::span<const uint8>(counterOneAck.data(), counterOneAck.size())));
	second.Process(2000000, BootOutbox(secondWire));
	SIM_EXPECT(second.Add(VUB::Meta{}, std::span<const uint8>(payload.data(), payload.size()),
		std::span<uint8>(out.data(), out.size())).IsErr());
	std::array<uint8, 4> secondAck = {0x12, 0x34, 0x2C, 0x01};
	SIM_EXPECT(BootTake(second, std::span<const uint8>(secondAck.data(), secondAck.size())));
	second.Process(2000000, BootOutbox(secondWire));
	SIM_EXPECT(BootAdd(second, 0x0002, secondWire));
	// The first data packet of boot 0x1234: number 0x012D, delta 0, id 2.
	std::array<uint8, 7> secondData = {0x01, 0x34, 0x2D, 0x01, 0x00, 0x02, 0x00};
	SIM_EXPECT(BootSentIs(secondWire, 0, secondData));

	// Boot 0x1235, counter 0: tag 0x35, number 0x9F63. The late acknowledgement of boot 0x1234's sync has another tag, so it is ignored.
	// The acknowledgement of this sync establishes the session.
	BootLayer third;
	third.SetSession(0x1235);
	BootSide thirdWire{};
	BootStartSession(third, thirdWire, 3000000);
	std::array<uint8, 8> thirdSync = {0x05, 0x35, 0x63, 0x9F, 0x00, 0x35, 0x12, 0x00};
	SIM_EXPECT(BootSentIs(thirdWire, 0, thirdSync));
	thirdWire.count = 0;
	std::array<uint8, 4> lateSecondAck = {0x12, 0x34, 0x2C, 0x01};
	SIM_EXPECT(BootTake(third, std::span<const uint8>(lateSecondAck.data(), lateSecondAck.size())));
	third.Process(3000000, BootOutbox(thirdWire));
	SIM_EXPECT(third.Add(VUB::Meta{}, std::span<const uint8>(payload.data(), payload.size()),
		std::span<uint8>(out.data(), out.size())).IsErr());
	std::array<uint8, 4> thirdAck = {0x12, 0x35, 0x63, 0x9F};
	SIM_EXPECT(BootTake(third, std::span<const uint8>(thirdAck.data(), thirdAck.size())));
	third.Process(3000000, BootOutbox(thirdWire));
	SIM_EXPECT(BootAdd(third, 0x0003, thirdWire));
	// The first data packet of boot 0x1235: number 0x9F64, delta 0, id 3.
	std::array<uint8, 7> thirdData = {0x01, 0x35, 0x64, 0x9F, 0x00, 0x03, 0x00};
	SIM_EXPECT(BootSentIs(thirdWire, 0, thirdData));
}


// The sixteen-bit numbers wrap in the first packets. Boot 0x10F2 has the first number 0xFFFE: the sync is numbered 0xFFFE and the data
// starts at 0xFFFF, so the window crosses the wrap at its second packet (number 0x0000) and third (0x0001). The receiver passes the
// packets up in order, and acknowledges each with its number.
void TestSixteenBitNumbersWrapInTheFirstPackets() {
	BootSide wire{};
	BootSide receiving{};
	BootLayer sender;
	BootLayer receiver;
	sender.SetSession(0x10F2);
	BootStartSession(sender, wire, 1000000);
	// The sync: flags 0x05, tag 0xF2, number 0xFFFE (LE FE FF), delta 0, boot 0x10F2 (LE F2 10), counter 0.
	std::array<uint8, 8> sync = {0x05, 0xF2, 0xFE, 0xFF, 0x00, 0xF2, 0x10, 0x00};
	SIM_EXPECT(BootSentIs(wire, 0, sync));
	SIM_EXPECT(BootCarry(wire, 0, receiver, receiving));
	wire.count = 0;
	receiver.Process(1000000, BootOutbox(receiving));
	// The bare acknowledgement of the sync: tag 0xF2, number 0xFFFE.
	std::array<uint8, 4> syncAck = {0x12, 0xF2, 0xFE, 0xFF};
	SIM_EXPECT(receiving.count == 1);
	SIM_EXPECT(BootSentIs(receiving, 0, syncAck));
	SIM_EXPECT(BootTakeAt(sender, receiving, 0));
	receiving.count = 0;
	sender.Process(1000000, BootOutbox(wire));

	// Three packets: numbers 0xFFFF, 0x0000 and 0x0001. The base is 0xFFFF, so the deltas are 0, 1 and 2.
	SIM_EXPECT(BootAdd(sender, 1, wire));
	SIM_EXPECT(BootAdd(sender, 2, wire));
	SIM_EXPECT(BootAdd(sender, 3, wire));
	std::array<uint8, 7> first = {0x01, 0xF2, 0xFF, 0xFF, 0x00, 0x01, 0x00};
	std::array<uint8, 7> second = {0x01, 0xF2, 0x00, 0x00, 0x01, 0x02, 0x00};
	std::array<uint8, 7> third = {0x01, 0xF2, 0x01, 0x00, 0x02, 0x03, 0x00};
	SIM_EXPECT(BootSentIs(wire, 0, first));
	SIM_EXPECT(BootSentIs(wire, 1, second));
	SIM_EXPECT(BootSentIs(wire, 2, third));
	for (std::size_t index = 0; index < 3; index++) {
		SIM_EXPECT(BootCarry(wire, index, receiver, receiving));
	}

	receiver.Process(1000000, BootOutbox(receiving));
	SIM_EXPECT(receiving.upCount == 3);
	SIM_EXPECT(receiving.up[0] == 1);
	SIM_EXPECT(receiving.up[1] == 2);
	SIM_EXPECT(receiving.up[2] == 3);
	std::array<uint8, 4> ackFFFF = {0x12, 0xF2, 0xFF, 0xFF};
	std::array<uint8, 4> ack0000 = {0x12, 0xF2, 0x00, 0x00};
	std::array<uint8, 4> ack0001 = {0x12, 0xF2, 0x01, 0x00};
	SIM_EXPECT(receiving.count == 3);
	SIM_EXPECT(BootSentIs(receiving, 0, ackFFFF));
	SIM_EXPECT(BootSentIs(receiving, 1, ack0000));
	SIM_EXPECT(BootSentIs(receiving, 2, ack0001));
}


// The eight-bit numbers wrap in the first packets. The first number of boot 0x10F2 is 0xFE (the low byte of 0xFFFE), so the sync is
// numbered 0xFE and the data starts at 0xFF, then 0x00 and 0x01. A plain receiver and an in-order receiver both pass the packets up in
// order and acknowledge them with their numbers. Every sync and packet is one byte shorter in its number.
template<typename Receiver>
void RunEightBitWrap() {
	BootSide wire{};
	BootSide receiving{};
	EightBitLayer sender;
	Receiver receiver;
	sender.SetSession(0x10F2);
	BootStartSession(sender, wire, 1000000);
	// The sync is seven bytes: flags, tag, number (one byte), delta, boot value, counter.
	std::array<uint8, 7> sync = {0x05, 0xF2, 0xFE, 0x00, 0xF2, 0x10, 0x00};
	SIM_EXPECT(BootSentIs(wire, 0, sync));
	SIM_EXPECT(BootCarry(wire, 0, receiver, receiving));
	wire.count = 0;
	receiver.Process(1000000, BootOutbox(receiving));
	std::array<uint8, 3> syncAck = {0x12, 0xF2, 0xFE};
	SIM_EXPECT(receiving.count == 1);
	SIM_EXPECT(BootSentIs(receiving, 0, syncAck));
	SIM_EXPECT(BootTakeAt(sender, receiving, 0));
	receiving.count = 0;
	sender.Process(1000000, BootOutbox(wire));

	SIM_EXPECT(BootAdd(sender, 1, wire));
	SIM_EXPECT(BootAdd(sender, 2, wire));
	SIM_EXPECT(BootAdd(sender, 3, wire));
	std::array<uint8, 6> first = {0x01, 0xF2, 0xFF, 0x00, 0x01, 0x00};
	std::array<uint8, 6> second = {0x01, 0xF2, 0x00, 0x01, 0x02, 0x00};
	std::array<uint8, 6> third = {0x01, 0xF2, 0x01, 0x02, 0x03, 0x00};
	SIM_EXPECT(BootSentIs(wire, 0, first));
	SIM_EXPECT(BootSentIs(wire, 1, second));
	SIM_EXPECT(BootSentIs(wire, 2, third));
	for (std::size_t index = 0; index < 3; index++) {
		SIM_EXPECT(BootCarry(wire, index, receiver, receiving));
	}

	receiver.Process(1000000, BootOutbox(receiving));
	SIM_EXPECT(receiving.upCount == 3);
	SIM_EXPECT(receiving.up[0] == 1);
	SIM_EXPECT(receiving.up[1] == 2);
	SIM_EXPECT(receiving.up[2] == 3);
	std::array<uint8, 3> ackFF = {0x12, 0xF2, 0xFF};
	std::array<uint8, 3> ack00 = {0x12, 0xF2, 0x00};
	std::array<uint8, 3> ack01 = {0x12, 0xF2, 0x01};
	SIM_EXPECT(receiving.count == 3);
	SIM_EXPECT(BootSentIs(receiving, 0, ackFF));
	SIM_EXPECT(BootSentIs(receiving, 1, ack00));
	SIM_EXPECT(BootSentIs(receiving, 2, ack01));
}


// The receiver takes the number of a sync as it comes: the session starts at it, and a copy of the sync is acknowledged with the same number.
void TestSessionStartsAtTheNumberOfTheSync() {
	BootLayer receiver;
	BootSide receiving;
	std::array<uint8, 8> sync = {0x05, 0x34, 0x00, 0x70, 0x00, 0x34, 0x12, 0x00};
	std::array<uint8, 7> data = {0x01, 0x34, 0x01, 0x70, 0x01, 0x09, 0x00};
	BootSide wire;
	(void)BootSend(&wire, sync, VUB::Meta{});
	(void)BootSend(&wire, data, VUB::Meta{});
	SIM_EXPECT(BootCarry(wire, 0, receiver, receiving));
	SIM_EXPECT(BootCarry(wire, 1, receiver, receiving));
	SIM_EXPECT(BootCarry(wire, 0, receiver, receiving));
	receiver.Process(1000000, BootOutbox(receiving));
	SIM_EXPECT(receiving.upCount == 1);
	SIM_EXPECT(receiving.up[0] == 9);
	std::array<uint8, 4> ackSync = {0x12, 0x34, 0x00, 0x70};
	std::array<uint8, 4> ackData = {0x12, 0x34, 0x01, 0x70};
	SIM_EXPECT(receiving.count == 3);
	SIM_EXPECT(BootSentIs(receiving, 0, ackSync));
	SIM_EXPECT(BootSentIs(receiving, 1, ackData));
	SIM_EXPECT(BootSentIs(receiving, 2, ackSync));
}


int main() {
	TestSyncNumberIsTheFirstNumberOfTheBoot();
	TestSessionStartsAtTheNumberOfTheSync();
	TestLateAckOfOlderBootSyncDoesNotEstablishTheNewSession();
	TestBootCounterWithThreeBoots();
	TestSixteenBitNumbersWrapInTheFirstPackets();
	RunEightBitWrap<EightBitLayer>();
	RunEightBitWrap<EightBitOrderLayer>();
	return SimFinish("TestVubReliableBoot");
}
