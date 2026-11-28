#include <VHAL.h>
#include <VhalCheck.h>
#include <Utilities/Buffer/PacketQueue.h>
#include <Utilities/Memory/Storage.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <random>
#include <span>
#include <vector>

// Host tests of PacketQueue in every access mode: the packets, their lengths and flags, the refusals (busy, messageTooLong, empty,
// outOfRange), DropFlagged in the modes that offer it, the critical sections of each mode, Memory::External with its three arrays, and
// a seeded model. The queue holds Slots packets in every mode.


namespace {


bool SameBytes(std::span<const uint8> actual, const std::vector<uint8>& expected) {
	if (actual.size() != expected.size()) {
		return false;
	}

	for (std::size_t index = 0; index < expected.size(); index++) {
		if (actual[index] != expected[index]) {
			return false;
		}
	}

	return true;
}


template <std::size_t SlotBytes, uint16 Slots, RingAccess Access>
void PushesAndLooksUpInEveryMode() {
	PacketQueue<SlotBytes, Slots, Memory::Inside, Access> queue;
	VHAL_CHECK(queue.Empty());
	VHAL_CHECK_EQ(queue.Count(), static_cast<uint16>(0));

	std::span<const uint8> packet;
	uint8 flags = 0;
	VHAL_CHECK_EQ(queue.Front(packet, flags), ResultStatus::empty);
	VHAL_CHECK_EQ(queue.Peek(0, packet, flags), ResultStatus::outOfRange);

	const uint8 first[] = {1, 2, 3};
	VHAL_CHECK_EQ(queue.Push(std::span<const uint8>(first, 3), 0x11), ResultStatus::ok);
	VHAL_CHECK_EQ(queue.Push(std::span<const uint8>(), 0x22), ResultStatus::ok);

	VHAL_CHECK_EQ(queue.Front(packet, flags), ResultStatus::ok);
	VHAL_CHECK(SameBytes(packet, {1, 2, 3}));
	VHAL_CHECK_EQ(flags, static_cast<uint8>(0x11));

	VHAL_CHECK_EQ(queue.Peek(1, packet, flags), ResultStatus::ok);
	VHAL_CHECK_EQ(packet.size(), static_cast<std::size_t>(0));
	VHAL_CHECK_EQ(flags, static_cast<uint8>(0x22));
	VHAL_CHECK_EQ(queue.Peek(2, packet, flags), ResultStatus::outOfRange);

	VHAL_CHECK_EQ(queue.Pop(), ResultStatus::ok);
	VHAL_CHECK_EQ(queue.Count(), static_cast<uint16>(1));
	VHAL_CHECK_EQ(queue.Front(packet, flags), ResultStatus::ok);
	VHAL_CHECK_EQ(packet.size(), static_cast<std::size_t>(0));
	VHAL_CHECK_EQ(queue.Pop(), ResultStatus::ok);
	VHAL_CHECK_EQ(queue.Pop(), ResultStatus::empty);
}


template <std::size_t SlotBytes, uint16 Slots, RingAccess Access>
void RefusesFullAndLongPackets() {
	PacketQueue<SlotBytes, Slots, Memory::Inside, Access> queue;
	std::vector<uint8> longPacket(SlotBytes + 1, 0x55);
	VHAL_CHECK_EQ(queue.Push(std::span<const uint8>(longPacket), 0), ResultStatus::messageTooLong);
	VHAL_CHECK(queue.Empty());

	std::vector<uint8> fullPacket(SlotBytes, 0x66);
	for (uint16 index = 0; index < Slots; index++) {
		VHAL_CHECK_EQ(queue.Push(std::span<const uint8>(fullPacket), static_cast<uint8>(index)), ResultStatus::ok);
	}
	VHAL_CHECK(queue.Full());
	VHAL_CHECK_EQ(queue.Count(), Slots);
	VHAL_CHECK_EQ(queue.Push(std::span<const uint8>(fullPacket), 0), ResultStatus::busy);
	VHAL_CHECK_EQ(queue.Push(std::span<const uint8>(longPacket), 0), ResultStatus::messageTooLong);

	std::span<const uint8> packet;
	uint8 flags = 0;
	VHAL_CHECK_EQ(queue.Peek(Slots - 1, packet, flags), ResultStatus::ok);
	VHAL_CHECK_EQ(flags, static_cast<uint8>(Slots - 1));
	VHAL_CHECK_EQ(queue.Drop(Slots), Slots);
	VHAL_CHECK(queue.Empty());
}


template <RingAccess Access>
void DropFlaggedKeepsOrder() {
	PacketQueue<4, 4, Memory::Inside, Access> queue;
	const uint8 one[] = {1, 2};
	const uint8 two[] = {3};
	const uint8 three[] = {4, 5, 6};
	const uint8 four[] = {7};
	VHAL_CHECK_EQ(queue.Push(std::span<const uint8>(one, 2), 0x01), ResultStatus::ok);
	VHAL_CHECK_EQ(queue.Push(std::span<const uint8>(two, 1), 0x00), ResultStatus::ok);
	VHAL_CHECK_EQ(queue.Push(std::span<const uint8>(three, 3), 0x00), ResultStatus::ok);
	VHAL_CHECK_EQ(queue.Push(std::span<const uint8>(four, 1), 0x01), ResultStatus::ok);

	VHAL_CHECK_EQ(queue.DropFlagged(0x01), static_cast<uint16>(2));
	VHAL_CHECK_EQ(queue.Count(), static_cast<uint16>(2));

	std::span<const uint8> packet;
	uint8 flags = 0;
	VHAL_CHECK_EQ(queue.Front(packet, flags), ResultStatus::ok);
	VHAL_CHECK(SameBytes(packet, {3}));
	VHAL_CHECK_EQ(queue.Peek(1, packet, flags), ResultStatus::ok);
	VHAL_CHECK(SameBytes(packet, {4, 5, 6}));

	// Pushes after the compaction wrap around the slots and keep their order.
	const uint8 five[] = {8, 9};
	VHAL_CHECK_EQ(queue.Push(std::span<const uint8>(five, 2), 0x00), ResultStatus::ok);
	VHAL_CHECK_EQ(queue.Pop(), ResultStatus::ok);
	VHAL_CHECK_EQ(queue.Front(packet, flags), ResultStatus::ok);
	VHAL_CHECK(SameBytes(packet, {4, 5, 6}));
	VHAL_CHECK_EQ(queue.Drop(5), static_cast<uint16>(2));
	VHAL_CHECK(queue.Empty());
}


void LockedDropFlaggedTakesOneSection() {
	PacketQueue<4, 4> queue;
	const uint8 one[] = {1};
	VHAL_CHECK_EQ(queue.Push(std::span<const uint8>(one, 1), 0x01), ResultStatus::ok);
	uint32 before = simCriticalEnters;
	VHAL_CHECK_EQ(queue.DropFlagged(0x01), static_cast<uint16>(1));
	VHAL_CHECK_EQ(simCriticalEnters - before, 1u);
}


// A caller that holds a section pushes, drops and pushes again. The queue takes no section of its own in callerLocked mode.
void CallerLockedDropFlaggedUnderHeldSection() {
	PacketQueue<4, 4, Memory::Inside, RingAccess::callerLocked> queue;
	const uint8 one[] = {1};
	const uint8 two[] = {2};
	const uint8 three[] = {3};

	System::CriticalSection(true);
	uint32 before = simCriticalEnters;
	VHAL_CHECK_EQ(queue.Push(std::span<const uint8>(one, 1), 0x01), ResultStatus::ok);
	VHAL_CHECK_EQ(queue.Push(std::span<const uint8>(two, 1), 0x00), ResultStatus::ok);
	VHAL_CHECK_EQ(queue.DropFlagged(0x01), static_cast<uint16>(1));
	VHAL_CHECK_EQ(queue.Push(std::span<const uint8>(three, 1), 0x00), ResultStatus::ok);
	VHAL_CHECK_EQ(simCriticalEnters - before, 0u);
	System::CriticalSection(false);

	std::span<const uint8> packet;
	uint8 flags = 0;
	VHAL_CHECK_EQ(queue.Front(packet, flags), ResultStatus::ok);
	VHAL_CHECK(SameBytes(packet, {2}));
	VHAL_CHECK_EQ(queue.Pop(), ResultStatus::ok);
	VHAL_CHECK_EQ(queue.Front(packet, flags), ResultStatus::ok);
	VHAL_CHECK(SameBytes(packet, {3}));
}


template <RingAccess Access>
void LengthsTakeTheSlotSize() {
	// 255 bytes keep the length in one byte; 256 bytes need two.
	PacketQueue<255, 2, Memory::Inside, Access> small;
	PacketQueue<256, 2, Memory::Inside, Access> large;
	std::vector<uint8> packet255(255, 0x0A);
	std::vector<uint8> packet256(256, 0x0B);
	VHAL_CHECK_EQ(small.Push(std::span<const uint8>(packet255), 0), ResultStatus::ok);
	VHAL_CHECK_EQ(large.Push(std::span<const uint8>(packet256), 0), ResultStatus::ok);

	std::span<const uint8> packet;
	uint8 flags = 0;
	VHAL_CHECK_EQ(small.Front(packet, flags), ResultStatus::ok);
	VHAL_CHECK_EQ(packet.size(), static_cast<std::size_t>(255));
	VHAL_CHECK_EQ(large.Front(packet, flags), ResultStatus::ok);
	VHAL_CHECK_EQ(packet.size(), static_cast<std::size_t>(256));
	VHAL_CHECK_EQ(packet[255], static_cast<uint8>(0x0B));
}


void ExternalArraysInCallerLocked() {
	// Four bytes per slot, two packets: three slots of bytes, lengths and flags.
	uint8 bytesMemory[12] = {};
	uint8 lengthMemory[3] = {};
	uint8 flagMemory[3] = {};

	PacketQueue<4, 2, Memory::External, RingAccess::callerLocked> queue{
		std::span<uint8>(bytesMemory),
		std::span<uint8>(lengthMemory),
		std::span<uint8>(flagMemory)};
	const uint8 one[] = {9, 8};
	VHAL_CHECK_EQ(queue.Push(std::span<const uint8>(one, 2), 0x03), ResultStatus::ok);
	VHAL_CHECK_EQ(queue.Count(), static_cast<uint16>(1));
	VHAL_CHECK_EQ(lengthMemory[0], static_cast<uint8>(2));
	VHAL_CHECK_EQ(flagMemory[0], static_cast<uint8>(0x03));
	VHAL_CHECK_EQ(bytesMemory[1], static_cast<uint8>(8));

	uint8 shortBytes[11] = {};
	PacketQueue<4, 2, Memory::External, RingAccess::callerLocked> notReady{
		std::span<uint8>(shortBytes),
		std::span<uint8>(lengthMemory),
		std::span<uint8>(flagMemory)};
	VHAL_CHECK_EQ(notReady.Push(std::span<const uint8>(one, 2), 0), ResultStatus::notReady);
}


template <RingAccess Access, uint16 Slots>
void RunModel(uint32 seed, uint32 steps) {
	constexpr std::size_t slotBytes = 8;
	struct Packet {
		std::vector<uint8> bytes;
		uint8 flags;
	};

	PacketQueue<slotBytes, Slots, Memory::Inside, Access> queue;
	std::deque<Packet> held;
	std::mt19937 random(seed);
	uint8 nextValue = 1;

	for (uint32 step = 0; step < steps; step++) {
		uint32 operation = random() % 10;

		if (operation < 3) {
			std::size_t length = random() % 10;
			Packet packet{std::vector<uint8>(length), static_cast<uint8>(random() % 2)};
			for (std::size_t index = 0; index < length; index++) {
				packet.bytes[index] = nextValue;
				nextValue++;
			}

			ResultStatus status = queue.Push(std::span<const uint8>(packet.bytes), packet.flags);
			if (length > slotBytes) {
				VHAL_CHECK_EQ(status, ResultStatus::messageTooLong);
			} else if (held.size() == Slots) {
				VHAL_CHECK_EQ(status, ResultStatus::busy);
			} else {
				VHAL_CHECK_EQ(status, ResultStatus::ok);
				held.push_back(packet);
			}
		} else if (operation == 3 || operation == 4) {
			ResultStatus status = queue.Pop();
			if (held.empty()) {
				VHAL_CHECK_EQ(status, ResultStatus::empty);
			} else {
				VHAL_CHECK_EQ(status, ResultStatus::ok);
				held.pop_front();
			}
		} else if (operation == 5) {
			uint16 count = static_cast<uint16>(random() % 3);
			uint16 expected = static_cast<uint16>(std::min<std::size_t>(count, held.size()));
			VHAL_CHECK_EQ(queue.Drop(count), expected);
			for (uint16 index = 0; index < expected; index++) {
				held.pop_front();
			}
		} else if (operation == 6) {
			std::span<const uint8> packet;
			uint8 flags = 0;
			uint16 index = static_cast<uint16>(random() % (Slots + 1));
			ResultStatus status = queue.Peek(index, packet, flags);
			if (index < held.size()) {
				VHAL_CHECK_EQ(status, ResultStatus::ok);
				VHAL_CHECK(SameBytes(packet, held[index].bytes));
				VHAL_CHECK_EQ(flags, held[index].flags);
			} else {
				VHAL_CHECK_EQ(status, ResultStatus::outOfRange);
			}
		} else if (operation == 7) {
			if constexpr (Access != RingAccess::singleWriter) {
				uint8 flag = 0x01;
				uint16 expected = 0;
				std::deque<Packet> kept;
				for (const Packet& packet : held) {
					if ((packet.flags & flag) != 0) {
						expected++;
					} else {
						kept.push_back(packet);
					}
				}

				VHAL_CHECK_EQ(queue.DropFlagged(flag), expected);
				held = kept;
			}
		} else if (operation == 8) {
			if constexpr (Access != RingAccess::singleWriter) {
				if (random() % 4 == 0) {
					queue.Clear();
					held.clear();
				}
			}
		} else if (operation == 9) {
			std::span<const uint8> packet;
			uint8 flags = 0;
			if (held.empty()) {
				VHAL_CHECK_EQ(queue.Front(packet, flags), ResultStatus::empty);
			} else {
				VHAL_CHECK_EQ(queue.Front(packet, flags), ResultStatus::ok);
				VHAL_CHECK(SameBytes(packet, held.front().bytes));
				VHAL_CHECK_EQ(flags, held.front().flags);
			}
		}

		VHAL_CHECK_EQ(queue.Count(), static_cast<uint16>(held.size()));
		VHAL_CHECK_EQ(queue.Full(), held.size() == Slots);
		VHAL_CHECK_EQ(queue.Empty(), held.empty());
	}
}


}


int main() {
	PushesAndLooksUpInEveryMode<8, 3, RingAccess::locked>();
	PushesAndLooksUpInEveryMode<8, 3, RingAccess::singleWriter>();
	PushesAndLooksUpInEveryMode<8, 3, RingAccess::callerLocked>();

	RefusesFullAndLongPackets<8, 4, RingAccess::locked>();
	RefusesFullAndLongPackets<8, 4, RingAccess::singleWriter>();
	RefusesFullAndLongPackets<8, 4, RingAccess::callerLocked>();

	DropFlaggedKeepsOrder<RingAccess::locked>();
	DropFlaggedKeepsOrder<RingAccess::callerLocked>();
	LockedDropFlaggedTakesOneSection();
	CallerLockedDropFlaggedUnderHeldSection();

	LengthsTakeTheSlotSize<RingAccess::locked>();
	LengthsTakeTheSlotSize<RingAccess::singleWriter>();
	LengthsTakeTheSlotSize<RingAccess::callerLocked>();

	ExternalArraysInCallerLocked();

	RunModel<RingAccess::locked, 1>(11, 3000);
	RunModel<RingAccess::locked, 5>(12, 3000);
	RunModel<RingAccess::singleWriter, 1>(13, 3000);
	RunModel<RingAccess::singleWriter, 5>(14, 3000);
	RunModel<RingAccess::callerLocked, 1>(15, 3000);
	RunModel<RingAccess::callerLocked, 5>(16, 3000);

	return VhalCheck::Finish("TestPacketQueue");
}
