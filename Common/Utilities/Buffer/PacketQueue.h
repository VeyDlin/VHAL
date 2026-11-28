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
// The length is uint8 when SlotBytes is 255 or less, and uint16 otherwise. The queue holds Slots packets in every mode. Its indexes are
// RingIndexes, which leave one slot empty, so each of its three arrays holds Slots + 1 entries: Memory::External needs that many.
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
	static_assert(Slots < std::numeric_limits<uint16>::max(), "PacketQueue keeps Slots + 1 indexes: Slots must be below 65535");


private:
	using LengthType = std::conditional_t<(SlotBytes <= 255), uint8, uint16>;

	static constexpr bool isLocked = Access == RingAccess::locked;
	static constexpr uint16 slotCount = static_cast<uint16>(Slots + 1);
	static constexpr std::size_t storageSlots = static_cast<std::size_t>(slotCount);

	static constexpr std::memory_order publishOrder =
		Access == RingAccess::singleWriter ? std::memory_order_release : std::memory_order_relaxed;
	static constexpr std::memory_order observeOrder =
		Access == RingAccess::singleWriter ? std::memory_order_acquire : std::memory_order_relaxed;

	using ByteStorage = typename Policy::template Storage<uint8, storageSlots * SlotBytes>;
	using LengthStorage = typename Policy::template Storage<LengthType, storageSlots>;
	using FlagStorage = typename Policy::template Storage<uint8, storageSlots>;

	ByteStorage bytes;
	LengthStorage lengths;
	FlagStorage flags;
	RingIndexes indexes;


private:
	bool Ready() const {
		return bytes.Ready() && lengths.Ready() && flags.Ready();
	}


	// The packet of a slot, in place. The slot must hold a packet.
	std::span<const uint8> PacketAt(uint16 slot) const {
		return std::span<const uint8>(bytes.Data() + static_cast<std::size_t>(slot) * SlotBytes, lengths.Data()[slot]);
	}


	// The slot of the oldest packet.
	uint16 ReadSlot() const {
		return indexes.tail.load(std::memory_order_relaxed);
	}


	// The slot where the next packet goes.
	uint16 WriteSlot() const {
		return indexes.head.load(std::memory_order_relaxed);
	}


	// Releases count packets from the front. The packets must be read before this call.
	void Consume(uint16 count) {
		indexes.tail.store(RingIndexes::Advance(ReadSlot(), count, slotCount), publishOrder);
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
		, indexes(other.indexes) {
		other.indexes.Reset();
	}


	PacketQueue& operator=(PacketQueue&& other) noexcept {
		if (this != &other) {
			bytes = std::move(other.bytes);
			lengths = std::move(other.lengths);
			flags = std::move(other.flags);
			indexes = other.indexes;
			other.indexes.Reset();
		}

		return *this;
	}


	// The memory of the three arrays, for Memory::External: bytes for (Slots + 1) * SlotBytes bytes, lengths and flags for Slots + 1
	// entries each.
	template <typename BytesArgument, typename LengthsArgument, typename FlagsArgument>
		requires(
			std::constructible_from<ByteStorage, BytesArgument> && std::constructible_from<LengthStorage, LengthsArgument>
			&& std::constructible_from<FlagStorage, FlagsArgument>)
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
		if (Count() == Slots) {
			LeaveSection();
			return ResultStatus::busy;
		}

		uint16 head = WriteSlot();
		std::copy(packet.begin(), packet.end(), bytes.Data() + static_cast<std::size_t>(head) * SlotBytes);
		lengths.Data()[head] = static_cast<LengthType>(packet.size());
		flags.Data()[head] = packetFlags;
		indexes.head.store(RingIndexes::Next(head, slotCount), publishOrder);
		LeaveSection();

		return ResultStatus::ok;
	}


	// Consumer: the oldest packet, in place. empty when the queue has no packet.
	ResultStatus Front(std::span<const uint8>& packet, uint8& packetFlags) const {
		EnterSection();
		if (Count() == 0) {
			LeaveSection();
			return ResultStatus::empty;
		}

		uint16 slot = ReadSlot();
		packet = PacketAt(slot);
		packetFlags = flags.Data()[slot];
		LeaveSection();

		return ResultStatus::ok;
	}


	// Consumer: the packet at index (0 is the oldest), in place. outOfRange when index is not held.
	ResultStatus Peek(uint16 index, std::span<const uint8>& packet, uint8& packetFlags) const {
		EnterSection();
		if (index >= Count()) {
			LeaveSection();
			return ResultStatus::outOfRange;
		}

		uint16 slot = RingIndexes::Advance(ReadSlot(), index, slotCount);
		packet = PacketAt(slot);
		packetFlags = flags.Data()[slot];
		LeaveSection();

		return ResultStatus::ok;
	}


	// Consumer: removes the oldest packet. empty when the queue has no packet.
	ResultStatus Pop() {
		EnterSection();
		if (Count() == 0) {
			LeaveSection();
			return ResultStatus::empty;
		}

		Consume(1);
		LeaveSection();

		return ResultStatus::ok;
	}


	// Consumer: removes up to count of the oldest packets and returns how many were removed.
	uint16 Drop(uint16 count) {
		EnterSection();
		uint16 available = Count();
		uint16 dropped = count < available ? count : available;
		Consume(dropped);
		LeaveSection();

		return dropped;
	}


	// Drops every packet whose flags have the flag bit, and keeps the others in their order, each moved down to the first free slot.
	// Returns how many were dropped. It writes the head, which the producer owns, so it exists in locked and callerLocked mode only. It
	// reads the head once, and a push during the call would be lost: locked mode holds the critical section across the call, and
	// callerLocked requires the caller to serialize the accesses.
	uint16 DropFlagged(uint8 flag) {
		static_assert(
			Access != RingAccess::singleWriter,
			"PacketQueue::DropFlagged writes the head, which the producer owns in singleWriter mode: the consumer drops with Drop(count)");

		EnterSection();
		uint16 head = indexes.head.load(std::memory_order_relaxed);
		uint16 readSlot = indexes.tail.load(std::memory_order_relaxed);
		uint16 writeSlot = readSlot;
		uint16 dropped = 0;

		while (readSlot != head) {
			if ((flags.Data()[readSlot] & flag) != 0) {
				dropped++;
			} else {
				if (writeSlot != readSlot) {
					LengthType length = lengths.Data()[readSlot];
					std::copy_n(
						bytes.Data() + static_cast<std::size_t>(readSlot) * SlotBytes,
						length,
						bytes.Data() + static_cast<std::size_t>(writeSlot) * SlotBytes);
					lengths.Data()[writeSlot] = length;
					flags.Data()[writeSlot] = flags.Data()[readSlot];
				}

				writeSlot = RingIndexes::Next(writeSlot, slotCount);
			}

			readSlot = RingIndexes::Next(readSlot, slotCount);
		}

		indexes.head.store(writeSlot, publishOrder);
		LeaveSection();

		return dropped;
	}


	// The number of packets held.
	uint16 Count() const {
		return RingIndexes::Held(indexes.head.load(observeOrder), indexes.tail.load(observeOrder), slotCount);
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
		indexes.Reset();
		LeaveSection();
	}
};
