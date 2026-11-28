#pragma once
#include <VHAL.h>
#include <Utilities/Buffer/RingIndexes.h>
#include <Utilities/Memory/Storage.h>
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>


// A queue of Slots packets. Each packet is at most SlotBytes bytes long, and each slot keeps the length of its packet and a flags byte.
// The length is uint8 when SlotBytes is 255 or less, and uint16 otherwise. The queue holds Slots packets in every mode. The locked and
// callerLocked modes keep a read index and a count, so their three arrays hold Slots entries each. The singleWriter mode keeps head and
// tail indexes (RingIndexes), which leave one slot empty, so its arrays hold Slots + 1 entries: Memory::External needs that many.
//
// Access works as for RingBuffer: locked takes a critical section for each access, singleWriter has one producer (Push) and one
// consumer (every other member except the counts), and callerLocked leaves the serialization to the caller.
//
// The spans from Front() and Peek() stay valid until the packet is popped or dropped. In locked mode the caller makes sure no other
// context pops meanwhile.
template <std::size_t SlotBytes, uint16 Slots, typename Policy = Memory::Inside, RingAccess Access = RingAccess::locked>
class PacketQueue {
	static_assert(SlotBytes > 0, "PacketQueue with slots of 0 bytes are forbidden");
	static_assert(Slots > 0, "PacketQueue with 0 slots is forbidden");
	static_assert(
		Access != RingAccess::singleWriter || Slots < std::numeric_limits<uint16>::max(),
		"A singleWriter PacketQueue keeps Slots + 1 indexes: Slots must be below 65535");


private:
	using LengthType = std::conditional_t<(SlotBytes <= 255), uint8, uint16>;
	using Cursor = RingCursor<Access == RingAccess::singleWriter>;

	static constexpr bool isLocked = Access == RingAccess::locked;
	static constexpr uint16 slotCount = Cursor::SlotCount(Slots);
	static constexpr std::size_t storageSlots = static_cast<std::size_t>(slotCount);

	using ByteStorage = typename Policy::template Storage<uint8, storageSlots * SlotBytes>;
	using LengthStorage = typename Policy::template Storage<LengthType, storageSlots>;
	using FlagStorage = typename Policy::template Storage<uint8, storageSlots>;

	ByteStorage bytes;
	LengthStorage lengths;
	FlagStorage flags;
	Cursor cursor;


	bool Ready() const {
		return bytes.Ready() && lengths.Ready() && flags.Ready();
	}


	// The packet of a slot, in place. The slot must hold a packet.
	std::span<const uint8> PacketAt(uint16 slot) const {
		return std::span<const uint8>(bytes.Data() + static_cast<std::size_t>(slot) * SlotBytes, lengths.Data()[slot]);
	}


	static void EnterSection() {
		if constexpr (isLocked) {
			System::CriticalSection(true);
		}
	}


	static void LeaveSection() {
		if constexpr (isLocked) {
			System::CriticalSection(false);
		}
	}


public:
	PacketQueue() {}


	PacketQueue(const PacketQueue&) = default;
	PacketQueue& operator=(const PacketQueue&) = default;


	// A move takes the packets with the storage. The source is left empty, so its indexes are reset with them.
	PacketQueue(PacketQueue&& other) noexcept
		: bytes(std::move(other.bytes))
		, lengths(std::move(other.lengths))
		, flags(std::move(other.flags))
		, cursor(other.cursor) {
		other.cursor.Reset();
	}


	PacketQueue& operator=(PacketQueue&& other) noexcept {
		if (this != &other) {
			bytes = std::move(other.bytes);
			lengths = std::move(other.lengths);
			flags = std::move(other.flags);
			cursor = other.cursor;
			other.cursor.Reset();
		}

		return *this;
	}


	// The memory of the three arrays, for Memory::External: bytes for slots * SlotBytes bytes, and lengths and flags for slots entries
	// each, where slots is Slots, or Slots + 1 in singleWriter mode.
	template <typename BytesArgument, typename LengthsArgument, typename FlagsArgument>
	PacketQueue(BytesArgument&& bytesArgument, LengthsArgument&& lengthsArgument, FlagsArgument&& flagsArgument)
		: bytes(std::forward<BytesArgument>(bytesArgument))
		, lengths(std::forward<LengthsArgument>(lengthsArgument))
		, flags(std::forward<FlagsArgument>(flagsArgument)) {
	}


	// Producer: puts a packet at the back. messageTooLong when the packet is longer than a slot, busy when the queue is full.
	ResultStatus Push(std::span<const uint8> packet, uint8 packetFlags) {
		if (packet.size() > SlotBytes) {
			return ResultStatus::messageTooLong;
		}

		if (!Ready()) {
			return ResultStatus::notReady;
		}

		EnterSection();
		uint16 head = 0;
		if (cursor.Room(Slots, head) == 0) {
			LeaveSection();
			return ResultStatus::busy;
		}

		std::copy(packet.begin(), packet.end(), bytes.Data() + static_cast<std::size_t>(head) * SlotBytes);
		lengths.Data()[head] = static_cast<LengthType>(packet.size());
		flags.Data()[head] = packetFlags;
		cursor.Produce(Slots, head, 1);
		LeaveSection();

		return ResultStatus::ok;
	}


	// Consumer: the oldest packet, in place. empty when the queue has no packet.
	ResultStatus Front(std::span<const uint8>& packet, uint8& packetFlags) const {
		EnterSection();
		uint16 slot = 0;
		if (cursor.Available(Slots, slot) == 0) {
			LeaveSection();
			return ResultStatus::empty;
		}

		packet = PacketAt(slot);
		packetFlags = flags.Data()[slot];
		LeaveSection();

		return ResultStatus::ok;
	}


	// Consumer: the packet at index (0 is the oldest), in place. outOfRange when index is not held.
	ResultStatus Peek(uint16 index, std::span<const uint8>& packet, uint8& packetFlags) const {
		EnterSection();
		uint16 slot = 0;
		if (index >= cursor.Available(Slots, slot)) {
			LeaveSection();
			return ResultStatus::outOfRange;
		}

		slot = Cursor::Advance(slot, index, slotCount);
		packet = PacketAt(slot);
		packetFlags = flags.Data()[slot];
		LeaveSection();

		return ResultStatus::ok;
	}


	// Consumer: removes the oldest packet. empty when the queue has no packet.
	ResultStatus Pop() {
		EnterSection();
		uint16 slot = 0;
		if (cursor.Available(Slots, slot) == 0) {
			LeaveSection();
			return ResultStatus::empty;
		}

		cursor.Consume(Slots, slot, 1);
		LeaveSection();

		return ResultStatus::ok;
	}


	// Consumer: removes up to count of the oldest packets and returns how many were removed.
	uint16 Drop(uint16 count) {
		EnterSection();
		uint16 slot = 0;
		uint16 available = cursor.Available(Slots, slot);
		uint16 dropped = count < available ? count : available;
		cursor.Consume(Slots, slot, dropped);
		LeaveSection();

		return dropped;
	}


	// Drops every packet whose flags have the flag bit, and keeps the others in their order, each shifted down to the first free slot.
	// Returns how many were dropped. It writes the count, which the producer owns in singleWriter mode, so it exists in locked and
	// callerLocked mode only. It reads the queue state once, and a push during the call would be lost: locked mode holds the critical
	// section across the call, and callerLocked requires the caller to serialize the accesses.
	uint16 DropFlagged(uint8 flag) {
		static_assert(
			Access != RingAccess::singleWriter,
			"PacketQueue::DropFlagged writes the head, which the producer owns in singleWriter mode: the consumer drops with Drop(count)");

		EnterSection();
		uint16 readSlot = 0;
		uint16 held = cursor.Available(Slots, readSlot);
		uint16 writeSlot = readSlot;
		uint16 slot = readSlot;
		uint16 kept = 0;

		for (uint16 index = 0; index < held; index++) {
			if ((flags.Data()[slot] & flag) == 0) {
				if (writeSlot != slot) {
					LengthType length = lengths.Data()[slot];
					std::copy_n(
						bytes.Data() + static_cast<std::size_t>(slot) * SlotBytes,
						length,
						bytes.Data() + static_cast<std::size_t>(writeSlot) * SlotBytes);
					lengths.Data()[writeSlot] = length;
					flags.Data()[writeSlot] = flags.Data()[slot];
				}

				writeSlot = Cursor::Advance(writeSlot, 1, slotCount);
				kept++;
			}

			slot = Cursor::Advance(slot, 1, slotCount);
		}

		cursor.SetCount(kept);
		LeaveSection();

		return static_cast<uint16>(held - kept);
	}


	// The number of packets held.
	uint16 Count() const {
		return cursor.Size(Slots);
	}


	bool Full() const {
		return Count() == Slots;
	}


	bool Empty() const {
		return Count() == 0;
	}


	// Empties the queue. It is the reset path: a reset must not keep the bytes of the old session.
	void Clear() {
		static_assert(
			Access != RingAccess::singleWriter,
			"PacketQueue::Clear is not offered in singleWriter mode: the consumer empties the queue with Drop(Count())");

		EnterSection();
		cursor.Reset();
		LeaveSection();
	}
};
