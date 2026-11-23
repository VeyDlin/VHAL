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

// The delivery layer's sessions under the path contract: the path keeps the send order and may lose or duplicate packets. Each case of
// the version 2 model check (sessions of one boot, a sync of an earlier session, a copy of an old packet, a window slid by a packet from
// far ahead) is run here as a deterministic test. Some of them replay a copy of a packet the path would have to hold for a whole session;
// the copy is passed to the receiver by hand, and the session rules must refuse it. Payloads start with a 16-bit id.


// The ids the receiving side passed up, counted per id; the outcomes the sending side reported, counted per id and by status.
inline std::array<uint16, 512> replayDelivered{};
inline std::array<uint16, 512> replayOutcomes{};
inline std::array<ResultStatus, 512> replayStatus{};
inline uint32 replayRestarts = 0;
inline uint32 replayNowUs = 0x0100;

// The tag and the counter of the last sync the test carried to the receiving side.
inline uint8 replayLastSyncTag = 0;
inline uint8 replayLastSyncCounter = 0;


void ReplayRestart() {
	replayRestarts++;
}


void ReplayOutcome(const VUB::Outcome& outcome) {
	uint16 id = VUB::GetLe16(outcome.payload.data());
	if (id < replayStatus.size()) {
		replayStatus[id] = outcome.status;
		replayOutcomes[id]++;
	}
}


void ResetReplayState() {
	replayDelivered.fill(0);
	replayOutcomes.fill(0);
	replayStatus.fill(ResultStatus::busy);
	replayRestarts = 0;
	replayNowUs = 0x0100;
	replayLastSyncTag = 0;
	replayLastSyncCounter = 0;
}


// The packets a side sent, kept as bytes.
struct ReplayWire {
	std::array<std::array<uint8, 64>, 16> packets{};
	std::array<std::size_t, 16> lengths{};
	std::size_t count = 0;
};


ResultStatus ReplayCapture(void* owner, std::span<const uint8> packet, const VUB::Meta&) {
	ReplayWire* wire = static_cast<ReplayWire*>(owner);
	if (wire->count < wire->packets.size()) {
		for (std::size_t index = 0; index < packet.size(); index++) {
			wire->packets[wire->count][index] = packet[index];
		}

		wire->lengths[wire->count] = packet.size();
	}

	wire->count++;
	return ResultStatus::ok;
}


VUB::Outbox ReplayOutbox(ReplayWire& wire) {
	return VUB::Outbox(&wire, &ReplayCapture, nullptr, nullptr);
}


// The options of the sending side: the outcome handler is set, the restart handler is not. A layer takes its options as its template
// argument, so the options of each window and sequence width the tests use are a constant of their own.
template<uint8 Window, uint8 SequenceBits>
constexpr VUB::ReliableOptions<40, 4> SenderOptionsOf{.window = Window, .sequenceBits = SequenceBits, .outcomeFn = &ReplayOutcome};


// The options of the receiving side: the restart handler is set.
template<uint8 Window, uint8 SequenceBits>
constexpr VUB::ReliableOptions<40, 4> ReceiverOptionsOf{.window = Window, .sequenceBits = SequenceBits, .restartFn = &ReplayRestart};


// The layers of the sending and receiving sides with those options.
template<uint8 Window, uint8 SequenceBits>
using SenderLayerOf = VUB::ReliableOf<40, 4, false, SenderOptionsOf<Window, SequenceBits>>;


template<uint8 Window, uint8 SequenceBits>
using ReceiverLayerOf = VUB::ReliableOf<40, 4, false, ReceiverOptionsOf<Window, SequenceBits>>;


// Hands every packet of from to the layer to. The packets that are passed up are counted by id; the sync's tag and counter are kept.
template<typename Layer>
void Carry(ReplayWire& from, Layer& to) {
	for (std::size_t index = 0; index < from.count && index < from.packets.size(); index++) {
		std::span<const uint8> packet(from.packets[index].data(), from.lengths[index]);
		if (packet[0] == 0x05 && packet.size() >= 8) {
			replayLastSyncTag = packet[1];
			replayLastSyncCounter = packet[7];
		}

		Result<VUB::Taken> taken = to.Take(VUB::Meta{}, packet);
		if (taken.IsOk() && taken.Value().deliver) {
			uint16 id = VUB::GetLe16(packet.data() + taken.Value().consumed);
			if (id < replayDelivered.size()) {
				replayDelivered[id]++;
			}
		}
	}

	from.count = 0;
}


// Hands one packet given as bytes to the layer, the way the path would.
template<std::size_t Size, typename Layer>
Result<VUB::Taken> Replay(Layer& layer, const std::array<uint8, Size>& bytes) {
	Result<VUB::Taken> taken = layer.Take(VUB::Meta{}, std::span<const uint8>(bytes.data(), bytes.size()));
	if (taken.IsOk() && taken.Value().deliver) {
		uint16 id = VUB::GetLe16(bytes.data() + taken.Value().consumed);
		if (id < replayDelivered.size()) {
			replayDelivered[id]++;
		}
	}

	return taken;
}


// Copies the bytes of packet index of a wire, which must be Size bytes long.
template<std::size_t Size>
std::array<uint8, Size> Copied(const ReplayWire& wire, std::size_t index) {
	std::array<uint8, Size> bytes{};
	SIM_EXPECT(index < wire.count && wire.lengths[index] == Size);
	for (std::size_t byteIndex = 0; byteIndex < Size; byteIndex++) {
		bytes[byteIndex] = wire.packets[index][byteIndex];
	}

	return bytes;
}


// Both sides run once at the current time, and each side's packets reach the other.
template<typename NearType, typename FarType>
void Round(NearType& near, ReplayWire& nearWire, FarType& far, ReplayWire& farWire) {
	near.Process(replayNowUs, ReplayOutbox(nearWire));
	Carry(nearWire, far);
	far.Process(replayNowUs, ReplayOutbox(farWire));
	Carry(farWire, near);
}


// Starts the session of the sending side, when it has none: the first Send is busy and starts it, and the sync and its acknowledgement
// go round. Returns the tag of the sync.
template<typename NearType, typename FarType>
uint8 Establish(NearType& near, ReplayWire& nearWire, FarType& far, ReplayWire& farWire) {
	near.Process(replayNowUs, ReplayOutbox(nearWire));
	std::array<uint8, 2> trigger = {0xFF, 0xFF};
	std::array<uint8, 64> out{};
	SIM_EXPECT(near.Add(VUB::Meta{}, std::span<const uint8>(trigger.data(), trigger.size()), std::span<uint8>(out.data(), out.size()))
		.IsErr());
	nearWire.count = 0;
	near.Process(replayNowUs, ReplayOutbox(nearWire));
	SIM_EXPECT(nearWire.count == 1);
	uint8 tag = nearWire.packets[0][1];
	Round(near, nearWire, far, farWire);
	Round(near, nearWire, far, farWire);
	return tag;
}


// Adds a packet with the id as its payload. Returns true when the layer took it.
template<typename Layer>
bool SendId(Layer& layer, uint16 id, ReplayWire& wire) {
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


// Sends the id and runs rounds until its outcome is reported: ok, retryExhausted or reset. Returns true when the send was taken.
template<typename NearType, typename FarType>
bool SendUntilOutcome(NearType& near, ReplayWire& nearWire, FarType& far, ReplayWire& farWire, uint16 id) {
	uint16 before = replayOutcomes[id];
	bool taken = false;
	for (std::size_t round = 0; round < 12 && replayOutcomes[id] == before; round++) {
		if (!taken) {
			taken = SendId(near, id, nearWire);
		}

		Round(near, nearWire, far, farWire);
	}

	return taken;
}


// Test 0 of the model check: a held-back sync of an earlier session, after a later session of the same boot is live. The stale sync is
// refused by its counter; its data is not passed up, and no restart is reported.
void TestHeldSyncOfAnEarlierSessionIsIgnored() {
	ResetReplayState();
	ReplayWire nearWire{};
	ReplayWire farWire{};
	ReceiverLayerOf<4, 16> far;
	SenderLayerOf<4, 16> near;
	near.SetSession(0x1111);
	uint8 tagA = Establish(near, nearWire, far, farWire);
	SIM_EXPECT(tagA == 0x11);
	std::array<uint8, 8> syncA = {0x05, 0x11, 0x00, 0x00, 0x00, 0x11, 0x11, 0x00};
	SIM_EXPECT(SendId(near, 1, nearWire));
	Round(near, nearWire, far, farWire);
	SIM_EXPECT(replayDelivered[1] == 1);
	SIM_EXPECT(SendId(near, 2, nearWire));
	std::array<uint8, 7> heldA = Copied<7>(nearWire, 0);
	nearWire.count = 0;

	// The resync for session A starts session B of the same boot: counter 1, tag 0x12.
	std::array<uint8, 2> resyncA = {0x08, 0x11};
	SIM_EXPECT(near.Take(VUB::Meta{}, std::span<const uint8>(resyncA.data(), resyncA.size())).IsOk());
	uint8 tagB = Establish(near, nearWire, far, farWire);
	SIM_EXPECT(tagB == 0x12);
	SIM_EXPECT(replayRestarts == 1);
	SIM_EXPECT(SendUntilOutcome(near, nearWire, far, farWire, 3));
	SIM_EXPECT(replayDelivered[3] == 1);
	SIM_EXPECT(replayStatus[3] == ResultStatus::ok);

	// The held sync of session A arrives late. It is older than the held counter: no reopen, no acknowledgement, nothing passed up. The
	// receiver answers with a counter reply: boot 0x1111, held counter 1.
	Result<VUB::Taken> staleSync = Replay(far, syncA);
	SIM_EXPECT(staleSync.IsOk());
	SIM_EXPECT(!staleSync.Value().deliver);
	far.Process(replayNowUs, ReplayOutbox(farWire));
	std::array<uint8, 4> counterReplyB = {0x18, 0x11, 0x11, 0x01};
	SIM_EXPECT(farWire.count == 1);
	SIM_EXPECT(farWire.lengths[0] == 4);
	SIM_EXPECT(Copied<4>(farWire, 0) == counterReplyB);
	farWire.count = 0;
	SIM_EXPECT(replayRestarts == 1);

	// The held data of session A (id 2) is refused by its tag, and the live session goes on.
	SIM_EXPECT(!Replay(far, heldA).Value().deliver);
	SIM_EXPECT(replayDelivered[2] == 0);
	SIM_EXPECT(SendUntilOutcome(near, nearWire, far, farWire, 4));
	SIM_EXPECT(replayDelivered[4] == 1);
	SIM_EXPECT(replayRestarts == 1);
}


// Test 1 of the model check: the sessions of one boot have their own tags, so the data of session 1 that is held back is refused in
// session 3 (two restarts later). The data of session 3 is delivered.
void TestSessionsOfOneBootHaveTheirOwnTags() {
	ResetReplayState();
	ReplayWire nearWire{};
	ReplayWire farWire{};
	ReceiverLayerOf<4, 16> far;
	SenderLayerOf<4, 16> near;
	near.SetSession(0x2222);
	uint8 tag1 = Establish(near, nearWire, far, farWire);
	SIM_EXPECT(SendId(near, 10, nearWire));
	std::array<uint8, 7> heldFirst = Copied<7>(nearWire, 0);
	nearWire.count = 0;

	std::array<uint8, 2> resync1 = {0x08, tag1};
	SIM_EXPECT(near.Take(VUB::Meta{}, std::span<const uint8>(resync1.data(), resync1.size())).IsOk());
	uint8 tag2 = Establish(near, nearWire, far, farWire);
	std::array<uint8, 2> resync2 = {0x08, tag2};
	SIM_EXPECT(near.Take(VUB::Meta{}, std::span<const uint8>(resync2.data(), resync2.size())).IsOk());
	uint8 tag3 = Establish(near, nearWire, far, farWire);
	SIM_EXPECT(tag1 == 0x22);
	SIM_EXPECT(tag2 == 0x23);
	SIM_EXPECT(tag3 == 0x24);
	SIM_EXPECT(replayOutcomes[10] == 1);
	SIM_EXPECT(replayStatus[10] == ResultStatus::reset);

	SIM_EXPECT(!Replay(far, heldFirst).Value().deliver);
	SIM_EXPECT(replayDelivered[10] == 0);
	SIM_EXPECT(SendUntilOutcome(near, nearWire, far, farWire, 13));
	SIM_EXPECT(replayDelivered[13] == 1);
	SIM_EXPECT(replayStatus[13] == ResultStatus::ok);
}


// Test 2 of the model check, with 8-bit numbers: a copy of packet 1 is passed to the receiver after the numbers have wrapped many times.
// The copy is behind the window and is not passed up; no later packet is acknowledged without being passed up.
void TestCopyOfAnOldPacketIsNotPassedUpAfterWraps() {
	ResetReplayState();
	ReplayWire nearWire{};
	ReplayWire farWire{};
	ReceiverLayerOf<4, 8> far;
	SenderLayerOf<4, 8> near;
	near.SetSession(0x3434);
	Establish(near, nearWire, far, farWire);
	SIM_EXPECT(SendId(near, 1, nearWire));
	// 8-bit numbers: flags, tag, one byte of number, one byte of delta, and the 2-byte id.
	std::array<uint8, 6> copyOfOne = Copied<6>(nearWire, 0);
	Round(near, nearWire, far, farWire);
	SIM_EXPECT(replayDelivered[1] == 1);
	for (uint16 id = 2; id < 300; id++) {
		SIM_EXPECT(SendUntilOutcome(near, nearWire, far, farWire, id));
	}

	SIM_EXPECT(!Replay(far, copyOfOne).Value().deliver);
	SIM_EXPECT(replayDelivered[1] == 1);
	for (uint16 id = 300; id < 310; id++) {
		SIM_EXPECT(SendUntilOutcome(near, nearWire, far, farWire, id));
	}

	for (uint16 id = 1; id < 310; id++) {
		SIM_EXPECT(replayDelivered[id] == 1);
		SIM_EXPECT(replayStatus[id] == ResultStatus::ok);
	}
}


// Test 3 of the model check: a sync of an earlier session with the same boot, while the live session is established, does not take the
// receiver off the live session. The live data goes up and no restart is reported.
void TestOlderSyncOfTheSameBootIsIgnoredWhileLive() {
	ResetReplayState();
	ReplayWire nearWire{};
	ReplayWire farWire{};
	ReceiverLayerOf<4, 16> far;
	SenderLayerOf<4, 16> near;
	near.SetSession(0x4444);
	uint8 tag1 = Establish(near, nearWire, far, farWire);
	std::array<uint8, 8> syncOne = {0x05, tag1, 0x00, 0x00, 0x00, 0x44, 0x44, 0x00};
	std::array<uint8, 2> resync = {0x08, tag1};
	SIM_EXPECT(near.Take(VUB::Meta{}, std::span<const uint8>(resync.data(), resync.size())).IsOk());
	uint8 tag2 = Establish(near, nearWire, far, farWire);
	SIM_EXPECT(tag2 == static_cast<uint8>(tag1 + 1));
	// The session that replaced the first one is a restart on the receiver.
	SIM_EXPECT(replayRestarts == 1);
	SIM_EXPECT(SendUntilOutcome(near, nearWire, far, farWire, 5));
	SIM_EXPECT(replayDelivered[5] == 1);

	Result<VUB::Taken> staleSync = Replay(far, syncOne);
	SIM_EXPECT(staleSync.IsOk());
	SIM_EXPECT(!staleSync.Value().deliver);
	far.Process(replayNowUs, ReplayOutbox(farWire));
	// Counter reply: boot 0x4444, held counter 1 (tag 0x45). The live sender ignores it.
	std::array<uint8, 4> counterReplyLive = {0x18, 0x44, 0x44, 0x01};
	SIM_EXPECT(farWire.count == 1);
	SIM_EXPECT(farWire.lengths[0] == 4);
	SIM_EXPECT(Copied<4>(farWire, 0) == counterReplyLive);
	farWire.count = 0;
	SIM_EXPECT(replayRestarts == 1);
	SIM_EXPECT(SendUntilOutcome(near, nearWire, far, farWire, 6));
	SIM_EXPECT(replayDelivered[6] == 1);
	SIM_EXPECT(replayStatus[6] == ResultStatus::ok);
}


// The serial arithmetic of the session counter: a sync 10 sessions ahead of the held one opens a session; a sync 196 sessions ahead is
// stale and is ignored; the held counter is then kept. The bytes are written by hand, boot 0x1234.
void TestSessionCounterWrapsWithSerialArithmetic() {
	ResetReplayState();
	ReceiverLayerOf<4, 16> far;
	// Counter 250 (0xFA): tag 0xFA + 0x34 = 0x2E.
	std::array<uint8, 8> sync250 = {0x05, 0x2E, 0x00, 0x00, 0x00, 0x34, 0x12, 0xFA};
	SIM_EXPECT(Replay(far, sync250).IsOk());
	ReplayWire farWire{};
	far.Process(replayNowUs, ReplayOutbox(farWire));
	std::array<uint8, 4> ackOf250 = {0x12, 0x2E, 0x00, 0x00};
	SIM_EXPECT(farWire.count == 1);
	SIM_EXPECT(farWire.lengths[0] == 4);
	SIM_EXPECT(Copied<4>(farWire, 0) == ackOf250);
	farWire.count = 0;

	// Counter 4 is 10 ahead of 250 (serial): a session that the receiver reports as a restart. Tag 0x38.
	std::array<uint8, 8> sync4 = {0x05, 0x38, 0x00, 0x00, 0x00, 0x34, 0x12, 0x04};
	SIM_EXPECT(Replay(far, sync4).IsOk());
	far.Process(replayNowUs, ReplayOutbox(farWire));
	SIM_EXPECT(replayRestarts == 1);
	std::array<uint8, 4> ackOf4 = {0x12, 0x38, 0x00, 0x00};
	SIM_EXPECT(farWire.count == 1);
	SIM_EXPECT(Copied<4>(farWire, 0) == ackOf4);
	farWire.count = 0;

	// Counter 200 is 196 behind 4 (serial): stale. It is ignored: no restart and no acknowledgement. Tag 0xFC.
	std::array<uint8, 8> sync200 = {0x05, 0xFC, 0x00, 0x00, 0x00, 0x34, 0x12, 0xC8};
	SIM_EXPECT(Replay(far, sync200).IsOk());
	SIM_EXPECT(!Replay(far, sync200).Value().deliver);
	far.Process(replayNowUs, ReplayOutbox(farWire));
	// One counter reply for both copies of the stale sync: boot 0x1234, held counter 4.
	std::array<uint8, 4> counterReply4 = {0x18, 0x34, 0x12, 0x04};
	SIM_EXPECT(farWire.count == 1);
	SIM_EXPECT(Copied<4>(farWire, 0) == counterReply4);
	farWire.count = 0;
	SIM_EXPECT(replayRestarts == 1);
}


// A session for each receiver reboot, 300 times, with one boot value: each session's sync carries the next counter, and its tag is the
// counter plus the boot value's low byte, so consecutive sessions have different tags for 256 sessions.
void TestTags300SessionsOfOneBoot() {
	ResetReplayState();
	ReplayWire nearWire{};
	ReplayWire farWire{};
	SenderLayerOf<4, 16> near;
	near.SetSession(0x1234);
	std::optional<ReceiverLayerOf<4, 16>> far;
	for (uint16 session = 0; session < 300; session++) {
		far.emplace();
		uint16 dummy = 500;
		if (session > 0) {
			SIM_EXPECT(SendUntilOutcome(near, nearWire, *far, farWire, dummy));
		}

		SIM_EXPECT(SendUntilOutcome(near, nearWire, *far, farWire, session % 500));
		SIM_EXPECT(replayLastSyncCounter == static_cast<uint8>(session));
		SIM_EXPECT(replayLastSyncTag == static_cast<uint8>(session + 0x34));
		SIM_EXPECT(replayDelivered[session % 500] >= 1);
	}
}


// Test G of the window base: a sender gives up on its packets (the path drops them), then sends again. The receiver moves its base to the
// sender's base, so the packets after them go up, and the given-up packets never go up, not even when their copies arrive later.
template<std::size_t Capacity>
void CheckWindowBaseAfterGiveUp() {
	// The window is the layer's whole window capacity in both uses of this check.
	constexpr uint8 window = static_cast<uint8>(Capacity);
	ResetReplayState();
	ReplayWire nearWire{};
	ReplayWire farWire{};
	constexpr VUB::ReliableOptions<40, Capacity> farOptions{.window = window, .sequenceBits = 16, .restartFn = &ReplayRestart};
	VUB::ReliableOf<40, Capacity, false, farOptions> far;
	constexpr VUB::ReliableOptions<40, Capacity> nearOptions{
		.retries = 1, .timeoutMs = 100, .window = window, .sequenceBits = 16, .outcomeFn = &ReplayOutcome};
	VUB::ReliableOf<40, Capacity, false, nearOptions> near;
	near.SetSession(0x5555);
	Establish(near, nearWire, far, farWire);

	ReplayWire held{};
	for (uint16 id = 1; id <= window; id++) {
		SIM_EXPECT(SendId(near, id, nearWire));
	}

	for (std::size_t index = 0; index < nearWire.count; index++) {
		held.packets[index] = nearWire.packets[index];
		held.lengths[index] = nearWire.lengths[index];
	}

	held.count = nearWire.count;
	nearWire.count = 0;
	for (uint32 step = 0; step < 100; step++) {
		replayNowUs += 10000;
		near.Process(replayNowUs, ReplayOutbox(nearWire));
		nearWire.count = 0;
	}

	for (uint16 id = 1; id <= window; id++) {
		SIM_EXPECT(replayOutcomes[id] == 1);
		SIM_EXPECT(replayStatus[id] == ResultStatus::retryExhausted);
	}

	SIM_EXPECT(SendUntilOutcome(near, nearWire, far, farWire, static_cast<uint16>(window + 1)));
	SIM_EXPECT(replayDelivered[window + 1] == 1);
	SIM_EXPECT(replayStatus[window + 1] == ResultStatus::ok);
	for (uint16 id = 1; id <= window; id++) {
		SIM_EXPECT(replayDelivered[id] == 0);
	}

	for (std::size_t index = 0; index < held.count; index++) {
		std::span<const uint8> packet(held.packets[index].data(), held.lengths[index]);
		Result<VUB::Taken> taken = far.Take(VUB::Meta{}, packet);
		SIM_EXPECT(taken.IsOk());
		SIM_EXPECT(!taken.Value().deliver);
	}

	for (uint16 id = static_cast<uint16>(window + 2); id <= 2 * window + 1; id++) {
		SIM_EXPECT(SendUntilOutcome(near, nearWire, far, farWire, id));
		SIM_EXPECT(replayDelivered[id] == 1);
	}
}


// Test E: a run with a sender reboot and a receiver reboot in the middle. Each id is delivered at most once; an id reported ok was passed
// up; the id in flight at the receiver's reboot is reported reset and not passed up.
void TestRebootsInTheMiddleOfARun() {
	ResetReplayState();
	ReplayWire nearWire{};
	ReplayWire farWire{};
	std::optional<ReceiverLayerOf<4, 16>> far;
	std::optional<SenderLayerOf<4, 16>> near;
	far.emplace();
	near.emplace();
	near->SetSession(0x1111);
	for (uint16 id = 0; id < 10; id++) {
		SIM_EXPECT(SendUntilOutcome(*near, nearWire, *far, farWire, id));
	}

	near.emplace();
	near->SetSession(0x2222);
	for (uint16 id = 10; id < 20; id++) {
		SIM_EXPECT(SendUntilOutcome(*near, nearWire, *far, farWire, id));
	}

	SIM_EXPECT(replayRestarts == 1);
	far.emplace();
	for (uint16 id = 20; id < 30; id++) {
		SIM_EXPECT(SendUntilOutcome(*near, nearWire, *far, farWire, id));
	}

	SIM_EXPECT(replayStatus[20] == ResultStatus::reset);
	SIM_EXPECT(replayDelivered[20] == 0);
	for (uint16 id = 21; id < 30; id++) {
		SIM_EXPECT(replayStatus[id] == ResultStatus::ok);
		SIM_EXPECT(replayDelivered[id] == 1);
	}

	for (uint16 id = 0; id < 30; id++) {
		SIM_EXPECT(replayDelivered[id] <= 1);
		SIM_EXPECT(replayStatus[id] != ResultStatus::ok || replayDelivered[id] == 1);
	}
}


int main() {
	TestHeldSyncOfAnEarlierSessionIsIgnored();
	TestSessionsOfOneBootHaveTheirOwnTags();
	TestCopyOfAnOldPacketIsNotPassedUpAfterWraps();
	TestOlderSyncOfTheSameBootIsIgnoredWhileLive();
	TestSessionCounterWrapsWithSerialArithmetic();
	TestTags300SessionsOfOneBoot();
	CheckWindowBaseAfterGiveUp<1>();
	CheckWindowBaseAfterGiveUp<8>();
	TestRebootsInTheMiddleOfARun();
	return SimFinish("TestVubReliableReplay");
}
