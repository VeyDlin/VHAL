#include "SimCheck.h"
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Bytes.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Outbox.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <optional>
#include <span>

// The session of the delivery layer, tested on the layer object: the layer is driven directly, one object per side, and the packets are
// carried between them by hand. A layer is rebuilt to model a reboot. Payloads start with a 16-bit id. The session starts with a sync
// packet of its own: a Send is busy until the sync is acknowledged, and the sync goes out at the next Process.

// The session tag is the session counter plus the low byte of the boot value. Expected bytes are written by hand from the layout on the wiki
// page.


using SessionLayer = VUB::ReliableOf<40, 4>;


// The restarts the receiving side reported.
inline uint32 testRestartCount = 0;


// The status and the count of the outcomes the sending side reported, by the id in each payload.
inline std::array<ResultStatus, 16> testStatusById{};
inline std::array<uint16, 16> testCountById{};


// The packets a side sent in one Process or Send, kept as bytes.
struct SessionWire {
	std::array<std::array<uint8, 64>, 16> packets{};
	std::array<std::size_t, 16> lengths{};
	std::size_t count = 0;
};


// The ids the receiving side passed up, in order.
struct SessionReceived {
	std::array<uint16, 16> ids{};
	std::size_t count = 0;
};


ResultStatus CaptureSend(void* owner, std::span<const uint8> packet, const VUB::Meta&) {
	SessionWire* wire = static_cast<SessionWire*>(owner);
	if (wire->count < wire->packets.size()) {
		for (std::size_t index = 0; index < packet.size(); index++) {
			wire->packets[wire->count][index] = packet[index];
		}

		wire->lengths[wire->count] = packet.size();
	}

	wire->count++;
	return ResultStatus::ok;
}


VUB::Outbox Outbox(SessionWire& wire) {
	return VUB::Outbox(&wire, &CaptureSend, nullptr, nullptr);
}


void CountRestart() {
	testRestartCount++;
}


void RecordById(const VUB::Outcome& outcome) {
	uint16 id = VUB::GetLe16(outcome.payload.data());
	if (id < testStatusById.size()) {
		testStatusById[id] = outcome.status;
		testCountById[id]++;
	}
}


// Adds a packet with the id as its payload. Returns true when the layer took it; false when it is busy, and then nothing is sent.
template<typename Layer>
bool SendId(Layer& layer, uint16 id, SessionWire& wire) {
	std::array<uint8, 2> payload{};
	VUB::PutLe16(payload.data(), id);
	std::array<uint8, 64> out{};
	Result<std::size_t> added = layer.Add(VUB::Meta{}, std::span<const uint8>(payload.data(), payload.size()),
		std::span<uint8>(out.data(), out.size()));
	if (added.IsErr()) {
		return false;
	}

	if (wire.count < wire.packets.size()) {
		for (std::size_t index = 0; index < added.Value(); index++) {
			wire.packets[wire.count][index] = out[index];
		}

		wire.lengths[wire.count] = added.Value();
	}

	wire.count++;
	return true;
}


// Hands every packet of from to the layer to. The payloads that go up are recorded in received; from is emptied.
template<typename Layer>
void Carry(SessionWire& from, Layer& to, SessionReceived& received) {
	for (std::size_t index = 0; index < from.count && index < from.packets.size(); index++) {
		std::span<const uint8> packet(from.packets[index].data(), from.lengths[index]);
		Result<VUB::Taken> taken = to.Take(VUB::Meta{}, packet);
		if (taken.IsOk() && taken.Value().deliver && received.count < received.ids.size()) {
			std::size_t at = taken.Value().consumed;
			received.ids[received.count] = VUB::GetLe16(packet.data() + at);
			received.count++;
		}
	}

	from.count = 0;
}


// One round between two layers at the same time: each side runs, and its packets reach the other.
template<typename NearType, typename FarType>
void Exchange(NearType& near, SessionWire& nearWire, FarType& far, SessionWire& farWire, SessionReceived& received, uint32 nowUs) {
	near.Process(nowUs, Outbox(nearWire));
	Carry(nearWire, far, received);
	far.Process(nowUs, Outbox(farWire));
	SessionReceived ignored{};
	Carry(farWire, near, ignored);
}


// Starts a session: the first Send is busy and starts it, and rounds carry the sync and its acknowledgement. Then the layer is ready.
template<typename NearType, typename FarType>
void StartSession(NearType& near, SessionWire& nearWire, FarType& far, SessionWire& farWire, SessionReceived& received, uint32 nowUs) {
	near.Process(nowUs, Outbox(nearWire));
	SIM_EXPECT(!SendId(near, 0xFFFF, nearWire));
	for (std::size_t round = 0; round < 3; round++) {
		Exchange(near, nearWire, far, farWire, received, nowUs);
	}
}


// The options of the far side and of the sending side. The layer takes them as its template argument, so they are constants of it.
constexpr VUB::ReliableOptions<40, 4> FarOptions{.restartFn = &VUB::RestartHandler<&CountRestart>};
constexpr VUB::ReliableOptions<40, 4> SendingOptions{.outcomeFn = &VUB::OutcomeHandler<&RecordById>};
using FarLayer = VUB::ReliableOf<40, 4, false, FarOptions>;
using SendingLayer = VUB::ReliableOf<40, 4, false, SendingOptions>;


// Takes a packet given as bytes, as the receiving side gets it.
template<typename Layer, std::size_t Size>
Result<VUB::Taken> TakeBytes(Layer& layer, const std::array<uint8, Size>& bytes) {
	return layer.Take(VUB::Meta{}, std::span<const uint8>(bytes.data(), bytes.size()));
}


// True when the wire holds exactly one packet, and it is the expected bytes.
template<std::size_t Size>
bool SentOnly(const SessionWire& wire, const std::array<uint8, Size>& expected) {
	if (wire.count != 1 || wire.lengths[0] != Size) {
		return false;
	}

	for (std::size_t index = 0; index < Size; index++) {
		if (wire.packets[0][index] != expected[index]) {
			return false;
		}
	}

	return true;
}


// The boot value is the session value the application gave. The sync is 0x05, the tag, the first number of the boot, the delta 0x00, the
// boot value and the counter 0x00.
void TestSessionValueSetsTheBootValue() {
	SessionWire wire{};
	SessionLayer layer;
	layer.SetSession(0x5A5A);
	SIM_EXPECT(!SendId(layer, 0, wire));
	layer.Process(0x0100, Outbox(wire));
	// The first number of boot 0x5A5A is 0xF556 (LE 56 F5).
	std::array<uint8, 8> sync = {0x05, 0x5A, 0x56, 0xF5, 0x00, 0x5A, 0x5A, 0x00};
	SIM_EXPECT(SentOnly(wire, sync));
}


// Without a session value the layer starts no session: Send returns noInit, and nothing goes out, not even on Process. Once the value is
// given, the first Send starts the session as usual.
void TestSendBeforeSessionValueIsNoInit() {
	SessionWire wire{};
	SessionLayer layer;
	std::array<uint8, 2> payload{};
	VUB::PutLe16(payload.data(), 0);
	std::array<uint8, 64> out{};
	Result<std::size_t> added = layer.Add(VUB::Meta{}, std::span<const uint8>(payload.data(), payload.size()),
		std::span<uint8>(out.data(), out.size()));
	SIM_EXPECT(added.IsErr());
	SIM_EXPECT(added.Error() == ResultStatus::noInit);
	layer.Process(0x1234, Outbox(wire));
	layer.Process(0x1234 + 300000, Outbox(wire));
	SIM_EXPECT(wire.count == 0);
	layer.SetSession(0x5A5A);
	SIM_EXPECT(!SendId(layer, 0, wire));
	layer.Process(0x0100, Outbox(wire));
	// The first number of boot 0x5A5A is 0xF556 (LE 56 F5).
	std::array<uint8, 8> sync = {0x05, 0x5A, 0x56, 0xF5, 0x00, 0x5A, 0x5A, 0x00};
	SIM_EXPECT(SentOnly(wire, sync));
}


// A layer that has sent nothing sends nothing on Process: a receiver never starts a session of its own.
void TestIdleLayerSendsNothing() {
	SessionWire wire{};
	SessionLayer layer;
	layer.Process(0x1234, Outbox(wire));
	layer.Process(0x1234 + 300000, Outbox(wire));
	SIM_EXPECT(wire.count == 0);
}


// Two boots with the same clock start, and different session values: the second session is a restart on the receiver, and its packets
// are delivered.
void TestSessionsWithSameClockAndDifferentValuesAreARestart() {
	testRestartCount = 0;
	SessionWire nearWire{};
	SessionWire farWire{};
	SessionReceived received{};
	FarLayer far;
	std::optional<SessionLayer> near;
	near.emplace();
	near->SetSession(0x1111);
	StartSession(*near, nearWire, far, farWire, received, 0x0100);
	SIM_EXPECT(SendId(*near, 0, nearWire));
	for (std::size_t round = 0; round < 3; round++) {
		Exchange(*near, nearWire, far, farWire, received, 0x0100);
	}

	SIM_EXPECT(received.count == 1);
	SIM_EXPECT(testRestartCount == 0);
	near.reset();
	near.emplace();
	near->SetSession(0x2222);
	StartSession(*near, nearWire, far, farWire, received, 0x0100);
	SIM_EXPECT(SendId(*near, 1, nearWire));
	for (std::size_t round = 0; round < 3; round++) {
		Exchange(*near, nearWire, far, farWire, received, 0x0100);
	}

	SIM_EXPECT(testRestartCount == 1);
	SIM_EXPECT(received.count == 2);
	SIM_EXPECT(received.ids[1] == 1);
}


// The sync arrives, and its acknowledgement is lost. The sender resends it with the same starting number: the receiver holds that
// session, so the copies are acknowledged and no restart is reported. When the acknowledgement gets through, data goes up.
void TestResentSyncOfTheSameSessionIsNotARestart() {
	testRestartCount = 0;
	SessionWire nearWire{};
	SessionWire farWire{};
	SessionReceived received{};
	FarLayer far;
	SessionLayer near;
	near.SetSession(0x3333);
	SIM_EXPECT(!SendId(near, 0, nearWire));
	near.Process(0x0100, Outbox(nearWire));
	Carry(nearWire, far, received);
	far.Process(0x0100, Outbox(farWire));
	farWire.count = 0;
	for (uint32 now = 0x0101; now <= 400000; now += 1000) {
		near.Process(now, Outbox(nearWire));
		Carry(nearWire, far, received);
		far.Process(now, Outbox(farWire));
		farWire.count = 0;
	}

	for (uint32 now = 400000; now <= 500000; now += 1000) {
		Exchange(near, nearWire, far, farWire, received, now);
	}

	SIM_EXPECT(SendId(near, 0, nearWire));
	for (uint32 now = 500000; now <= 501000; now += 1000) {
		Exchange(near, nearWire, far, farWire, received, now);
	}

	SIM_EXPECT(received.count == 1);
	SIM_EXPECT(received.ids[0] == 0);
	SIM_EXPECT(testRestartCount == 0);
}


// A packet of another session, six numbers ahead of the window, does not move the window and is not acknowledged. The receiver asks for
// the other session's tag to be resent: a resync request that carries that tag. The session's own packet after that is still delivered.
void TestOldSessionPacketCannotSlideTheWindow() {
	testRestartCount = 0;
	SessionWire farWire{};
	FarLayer far;
	far.Process(0x0100, Outbox(farWire));
	// The sync of boot 0x1234: number 0x012C (LE 2C 01), tag 0x34.
	std::array<uint8, 8> sync = {0x05, 0x34, 0x2C, 0x01, 0x00, 0x34, 0x12, 0x00};
	SIM_EXPECT(TakeBytes(far, sync).IsOk());
	far.Process(0x0100, Outbox(farWire));
	farWire.count = 0;
	// Flags 0x01, tag 0x77, number 0x123B, delta 0x00, payload id 0x00AA.
	std::array<uint8, 7> stale = {0x01, 0x77, 0x3B, 0x12, 0x00, 0xAA, 0x00};
	Result<VUB::Taken> taken = TakeBytes(far, stale);
	SIM_EXPECT(taken.IsOk());
	SIM_EXPECT(!taken.Value().deliver);
	far.Process(0x0100, Outbox(farWire));
	std::array<uint8, 2> resync = {0x08, 0x77};
	SIM_EXPECT(SentOnly(farWire, resync));
	// The session's own packet with the first data number 0x012D is inside the window and goes up.
	std::array<uint8, 7> current = {0x01, 0x34, 0x2D, 0x01, 0x00, 0x05, 0x00};
	Result<VUB::Taken> own = TakeBytes(far, current);
	SIM_EXPECT(own.IsOk());
	SIM_EXPECT(own.Value().deliver);
	SIM_EXPECT(testRestartCount == 0);
}


// Data of another session, with a number inside the window of the session the receiver holds, is not delivered: its tag is not the
// session's tag. It gets a resync request for its tag. The data goes up once the sync of its session is taken, and its copies do not.
void TestDataOfAnotherSessionIsNotDelivered() {
	testRestartCount = 0;
	SessionWire farWire{};
	FarLayer far;
	std::array<uint8, 8> sync = {0x05, 0x34, 0x2C, 0x01, 0x00, 0x34, 0x12, 0x00};
	SIM_EXPECT(TakeBytes(far, sync).IsOk());
	far.Process(0x0100, Outbox(farWire));
	farWire.count = 0;
	// The sync of session 0x5656 was lost; its data, with the number 0x012D (inside the window of the held session) and the tag 0x56, is
	// refused.
	std::array<uint8, 7> foreign = {0x01, 0x56, 0x2D, 0x01, 0x00, 0x05, 0x00};
	Result<VUB::Taken> refused = TakeBytes(far, foreign);
	SIM_EXPECT(refused.IsOk());
	SIM_EXPECT(!refused.Value().deliver);
	far.Process(0x0100, Outbox(farWire));
	std::array<uint8, 2> resync = {0x08, 0x56};
	SIM_EXPECT(SentOnly(farWire, resync));
	farWire.count = 0;

	// The sync of boot 0x5656, counter 0: the tag is 0x56, the number is 0xA07A (LE 7A A0).
	std::array<uint8, 8> newSync = {0x05, 0x56, 0x7A, 0xA0, 0x00, 0x56, 0x56, 0x00};
	SIM_EXPECT(TakeBytes(far, newSync).IsOk());
	// The restart handler runs in Process, after the packet is taken.
	far.Process(0x0100, Outbox(farWire));
	SIM_EXPECT(testRestartCount == 1);
	// The first data packet of session 0x5656: number 0xA07B, delta 0.
	std::array<uint8, 7> next = {0x01, 0x56, 0x7B, 0xA0, 0x00, 0x06, 0x00};
	Result<VUB::Taken> first = TakeBytes(far, next);
	SIM_EXPECT(first.IsOk());
	SIM_EXPECT(first.Value().deliver);
	Result<VUB::Taken> copy = TakeBytes(far, next);
	SIM_EXPECT(copy.IsOk());
	SIM_EXPECT(!copy.Value().deliver);
}


// A resync request names a session. One for another tag is ignored. One for the sender's current tag resets the packets in flight.
void TestResyncOfAnotherSessionIsIgnored() {
	testCountById.fill(0);
	SessionWire nearWire{};
	SessionWire farWire{};
	SessionReceived received{};
	FarLayer far;
	SendingLayer near;
	near.SetSession(0x1234);
	StartSession(near, nearWire, far, farWire, received, 0x0100);
	SIM_EXPECT(SendId(near, 3, nearWire));
	nearWire.count = 0;
	std::array<uint8, 2> other = {0x08, 0x77};
	SIM_EXPECT(near.Take(VUB::Meta{}, std::span<const uint8>(other.data(), other.size())).IsOk());
	near.Process(0x0100, Outbox(nearWire));
	SIM_EXPECT(testCountById[3] == 0);
	std::array<uint8, 2> own = {0x08, 0x34};
	SIM_EXPECT(near.Take(VUB::Meta{}, std::span<const uint8>(own.data(), own.size())).IsOk());
	near.Process(0x0100, Outbox(nearWire));
	SIM_EXPECT(testCountById[3] == 1);
	SIM_EXPECT(testStatusById[3] == ResultStatus::reset);
}


// A resync request for the current session, delayed past the sync's acknowledgement (only a reordering link does that), resets the
// packets in flight and starts the session that follows it. Nothing is delivered twice: the packets that were delivered and the packet that
// was lost are reported once each, the lost one as reset, and the packets of the following session are delivered once each.
void TestDelayedResyncOfTheCurrentSessionIsSafe() {
	testRestartCount = 0;
	testCountById.fill(0);
	testStatusById.fill(ResultStatus::busy);
	SessionWire nearWire{};
	SessionWire farWire{};
	SessionReceived received{};
	FarLayer far;
	SendingLayer near;
	near.SetSession(0x1234);
	StartSession(near, nearWire, far, farWire, received, 0x0100);
	SIM_EXPECT(SendId(near, 0, nearWire));
	Exchange(near, nearWire, far, farWire, received, 0x0100);
	Exchange(near, nearWire, far, farWire, received, 0x0100);
	Exchange(near, nearWire, far, farWire, received, 0x0100);
	SIM_EXPECT(SendId(near, 1, nearWire));
	near.Process(0x0100, Outbox(nearWire));
	Carry(nearWire, far, received);
	far.Process(0x0100, Outbox(farWire));
	farWire.count = 0;
	SIM_EXPECT(SendId(near, 2, nearWire));
	nearWire.count = 0;
	std::array<uint8, 2> delayed = {0x08, 0x34};
	SIM_EXPECT(near.Take(VUB::Meta{}, std::span<const uint8>(delayed.data(), delayed.size())).IsOk());
	near.Process(0x0100, Outbox(nearWire));
	SIM_EXPECT(testCountById[1] == 1);
	SIM_EXPECT(testStatusById[1] == ResultStatus::reset);
	SIM_EXPECT(testCountById[2] == 1);
	SIM_EXPECT(testStatusById[2] == ResultStatus::reset);
	SIM_EXPECT(!SendId(near, 3, nearWire));
	for (std::size_t round = 0; round < 3; round++) {
		Exchange(near, nearWire, far, farWire, received, 0x0100);
	}

	SIM_EXPECT(SendId(near, 3, nearWire));
	for (std::size_t round = 0; round < 3; round++) {
		Exchange(near, nearWire, far, farWire, received, 0x0100);
	}

	SIM_EXPECT(received.count == 3);
	SIM_EXPECT(received.ids[0] == 0);
	SIM_EXPECT(received.ids[1] == 1);
	SIM_EXPECT(received.ids[2] == 3);
	SIM_EXPECT(testCountById[0] == 1);
	SIM_EXPECT(testStatusById[0] == ResultStatus::ok);
	SIM_EXPECT(testCountById[3] == 1);
	SIM_EXPECT(testStatusById[3] == ResultStatus::ok);
	SIM_EXPECT(testRestartCount == 1);
}


// An acknowledgement of an old session that has the number of a packet in flight does not acknowledge it: its tag is another session's.
// The acknowledgement with the session's own tag does.
void TestStaleAcknowledgementDoesNotAcknowledge() {
	testCountById.fill(0);
	testStatusById.fill(ResultStatus::busy);
	SessionWire nearWire{};
	SessionWire farWire{};
	SessionReceived received{};
	FarLayer far;
	SendingLayer near;
	near.SetSession(0x1234);
	StartSession(near, nearWire, far, farWire, received, 0x0100);
	SIM_EXPECT(SendId(near, 0, nearWire));
	nearWire.count = 0;
	// Bare acknowledgement: flags 0x12, tag 0x77, number 0x012D (the number of the packet in flight).
	std::array<uint8, 4> stale = {0x12, 0x77, 0x2D, 0x01};
	SIM_EXPECT(TakeBytes(near, stale).IsOk());
	near.Process(0x0100, Outbox(nearWire));
	SIM_EXPECT(testCountById[0] == 0);
	// Bare acknowledgement: flags 0x12, tag 0x34, number 0x012D.
	std::array<uint8, 4> own = {0x12, 0x34, 0x2D, 0x01};
	SIM_EXPECT(TakeBytes(near, own).IsOk());
	near.Process(0x0100, Outbox(nearWire));
	SIM_EXPECT(testCountById[0] == 1);
	SIM_EXPECT(testStatusById[0] == ResultStatus::ok);
}


// The receiver holds counter 2 of boot 0x0100, and a sender of that boot starts at counter 0, behind the held counter. Its sync is stale
// to the receiver, which answers with a counter reply (0x18, the held boot value, the held counter). The sender moves past the held
// counter, and its next sync opens the session. The bytes are written by hand from the wiki's layout.
void TestCounterReplyMovesTheSenderPastTheHeldCounter() {
	testRestartCount = 0;
	testCountById.fill(0);
	testStatusById.fill(ResultStatus::busy);
	SessionWire nearWire{};
	SessionWire farWire{};
	SessionReceived received{};
	FarLayer far;
	// A sync of boot 0x0100 with counter 2 and tag 0x02, from an earlier life of the sender.
	// The first number of boot 0x0100 is 0x3700 (LE 00 37).
	std::array<uint8, 8> earlier = {0x05, 0x02, 0x00, 0x37, 0x00, 0x00, 0x01, 0x02};
	SIM_EXPECT(TakeBytes(far, earlier).IsOk());
	far.Process(0x0100, Outbox(farWire));
	// Bare acknowledgement of the sync: flags 0x12, tag 0x02, number 0x3700.
	std::array<uint8, 4> earlierAck = {0x12, 0x02, 0x00, 0x37};
	SIM_EXPECT(SentOnly(farWire, earlierAck));
	farWire.count = 0;
	SendingLayer near;
	near.SetSession(0x0100);
	SIM_EXPECT(!SendId(near, 1, nearWire));
	near.Process(0x0100, Outbox(nearWire));
	// The sync of the rebooted sender: boot 0x0100, counter 0, tag 0x00.
	std::array<uint8, 8> firstSync = {0x05, 0x00, 0x00, 0x37, 0x00, 0x00, 0x01, 0x00};
	SIM_EXPECT(SentOnly(nearWire, firstSync));
	nearWire.count = 0;
	SIM_EXPECT(TakeBytes(far, firstSync).IsOk());
	far.Process(0x0100, Outbox(farWire));
	// Counter reply: flags 0x18, boot 0x0100 little-endian, held counter 2.
	std::array<uint8, 4> reply = {0x18, 0x00, 0x01, 0x02};
	SIM_EXPECT(SentOnly(farWire, reply));
	farWire.count = 0;
	SIM_EXPECT(TakeBytes(near, reply).IsOk());
	near.Process(0x0100, Outbox(nearWire));
	// The sync moves on to counter 3, tag 0x03.
	std::array<uint8, 8> nextSync = {0x05, 0x03, 0x00, 0x37, 0x00, 0x00, 0x01, 0x03};
	SIM_EXPECT(SentOnly(nearWire, nextSync));
	nearWire.count = 0;
	SIM_EXPECT(TakeBytes(far, nextSync).IsOk());
	far.Process(0x0100, Outbox(farWire));
	SIM_EXPECT(testRestartCount == 1);
	// Bare acknowledgement of the sync: flags 0x12, tag 0x03, number 0x3700.
	std::array<uint8, 4> nextAck = {0x12, 0x03, 0x00, 0x37};
	SIM_EXPECT(SentOnly(farWire, nextAck));
	farWire.count = 0;
	SIM_EXPECT(TakeBytes(near, nextAck).IsOk());
	SIM_EXPECT(SendId(near, 1, nearWire));
	near.Process(0x0100, Outbox(nearWire));
	// Data: flags 0x01, tag 0x03, number 0x3701, delta 0, payload 01 00.
	std::array<uint8, 7> data = {0x01, 0x03, 0x01, 0x37, 0x00, 0x01, 0x00};
	SIM_EXPECT(SentOnly(nearWire, data));
	Carry(nearWire, far, received);
	SIM_EXPECT(received.count == 1);
	SIM_EXPECT(received.ids[0] == 1);
}


// A sync of an older counter of a live sender: the receiver answers with its counter reply, which the established sender ignores. The
// session goes on: no send is busy, nothing is restarted, and the data is delivered.
void TestStaleSyncOfLiveSenderDoesNotDisturb() {
	testRestartCount = 0;
	testCountById.fill(0);
	testStatusById.fill(ResultStatus::busy);
	SessionWire nearWire{};
	SessionWire farWire{};
	SessionReceived received{};
	FarLayer far;
	SendingLayer near;
	near.SetSession(0x0100);
	StartSession(near, nearWire, far, farWire, received, 0x0100);
	SIM_EXPECT(SendId(near, 0, nearWire));
	for (std::size_t round = 0; round < 3; round++) {
		Exchange(near, nearWire, far, farWire, received, 0x0100);
	}

	SIM_EXPECT(testStatusById[0] == ResultStatus::ok);
	// A sync of boot 0x0100 with counter 0xFF, tag 0xFF: 255 behind the held counter 0, so the receiver takes it as stale.
	std::array<uint8, 8> stale = {0x05, 0xFF, 0x00, 0x37, 0x00, 0x00, 0x01, 0xFF};
	SIM_EXPECT(TakeBytes(far, stale).IsOk());
	far.Process(0x0100, Outbox(farWire));
	// Counter reply: flags 0x18, boot 0x0100, held counter 0.
	std::array<uint8, 4> reply = {0x18, 0x00, 0x01, 0x00};
	SIM_EXPECT(SentOnly(farWire, reply));
	farWire.count = 0;
	SIM_EXPECT(TakeBytes(near, reply).IsOk());
	near.Process(0x0100, Outbox(nearWire));
	SIM_EXPECT(nearWire.count == 0);
	SIM_EXPECT(SendId(near, 1, nearWire));
	for (std::size_t round = 0; round < 3; round++) {
		Exchange(near, nearWire, far, farWire, received, 0x0100);
	}

	SIM_EXPECT(testRestartCount == 0);
	SIM_EXPECT(testStatusById[1] == ResultStatus::ok);
	SIM_EXPECT(received.count == 2);
	SIM_EXPECT(received.ids[1] == 1);
}


// A counter reply for another boot value is not for this sender: its sync is not moved, and the same sync goes out again when it times out.
void TestCounterReplyOfAnotherBootIsIgnored() {
	SessionWire wire{};
	SendingLayer near;
	near.SetSession(0x0200);
	SIM_EXPECT(!SendId(near, 0, wire));
	near.Process(0x0100, Outbox(wire));
	// Sync of boot 0x0200, counter 0, tag 0x00: the first number of boot 0x0200 is 0x6E00 (LE 00 6E).
	std::array<uint8, 8> sync = {0x05, 0x00, 0x00, 0x6E, 0x00, 0x00, 0x02, 0x00};
	SIM_EXPECT(SentOnly(wire, sync));
	wire.count = 0;
	// Counter reply of boot 0x0100, held counter 2.
	std::array<uint8, 4> reply = {0x18, 0x00, 0x01, 0x02};
	SIM_EXPECT(TakeBytes(near, reply).IsOk());
	near.Process(0x0100, Outbox(wire));
	SIM_EXPECT(wire.count == 0);
	// The sync times out at 200 ms and goes out again with the same counter.
	near.Process(0x0100 + 200000, Outbox(wire));
	SIM_EXPECT(SentOnly(wire, sync));
}


int main() {
	TestCounterReplyMovesTheSenderPastTheHeldCounter();
	TestStaleSyncOfLiveSenderDoesNotDisturb();
	TestCounterReplyOfAnotherBootIsIgnored();
	TestSessionValueSetsTheBootValue();
	TestSendBeforeSessionValueIsNoInit();
	TestIdleLayerSendsNothing();
	TestSessionsWithSameClockAndDifferentValuesAreARestart();
	TestResentSyncOfTheSameSessionIsNotARestart();
	TestOldSessionPacketCannotSlideTheWindow();
	TestDataOfAnotherSessionIsNotDelivered();
	TestResyncOfAnotherSessionIsIgnored();
	TestDelayedResyncOfTheCurrentSessionIsSafe();
	TestStaleAcknowledgementDoesNotAcknowledge();
	return SimFinish("TestVubReliableSession");
}
