#pragma once
#include <Drivers/Interface/VUB/Ring.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Memory/Storage.h>
#include <cstddef>
#include <span>

namespace VUB {


// Storage that exists only when the stack has a use for it. A disabled storage has no members, so a member of that type declared
// [[no_unique_address]] takes no RAM. Tag gives each storage its own type, so two disabled ones may share an address. The bytes of a buffer
// are not set when the stack is built: they are written ahead of each read, and the indexes say which of them are.
//
// The elements of an enabled storage are kept in Policy::Storage, a memory policy of Memory (Memory::Inside by default, which keeps them in
// the stack object). The accessors give spans of the elements, whatever the policy.


// A byte buffer of Bytes bytes, or nothing.
template<bool Enabled, std::size_t Bytes, std::size_t Tag, typename Policy = Memory::Inside>
struct Buffer {
	typename Policy::template Storage<uint8, Bytes> bytes;


	bool Ready() const {
		return bytes.Ready();
	}


	std::span<uint8> Span() {
		return std::span<uint8>(bytes.Data(), Bytes);
	}


	std::span<const uint8> Span() const {
		return std::span<const uint8>(bytes.Data(), Bytes);
	}
};


template<std::size_t Bytes, std::size_t Tag, typename Policy>
struct Buffer<false, Bytes, Tag, Policy> {
	constexpr bool Ready() const {
		return true;
	}


	std::span<uint8> Span() {
		return std::span<uint8>();
	}


	std::span<const uint8> Span() const {
		return std::span<const uint8>();
	}
};


// A value of type T, or nothing.
template<bool Enabled, typename T, std::size_t Tag>
struct State {
	T value{};


	T& Get() {
		return value;
	}


	const T& Get() const {
		return value;
	}
};


template<typename T, std::size_t Tag>
struct State<false, T, Tag> {
};


// A byte ring of Slots slots (Slots - 1 bytes are held), or nothing.
template<bool Enabled, std::size_t Slots, std::size_t Tag, typename Policy = Memory::Inside>
struct RingStorage {
	typename Policy::template Storage<uint8, Slots> slots;
	Indexes indexes;


	bool Ready() const {
		return slots.Ready();
	}


	std::span<uint8> Span() {
		return std::span<uint8>(slots.Data(), Slots);
	}
};


template<std::size_t Slots, std::size_t Tag, typename Policy>
struct RingStorage<false, Slots, Tag, Policy> {
	constexpr bool Ready() const {
		return true;
	}
};


// The receive time of each byte of a ring of Slots slots, or nothing. Only a chain whose framing uses time has it.
template<bool Enabled, std::size_t Slots, std::size_t Tag, typename Policy = Memory::Inside>
struct StampStorage {
	typename Policy::template Storage<uint32, Slots> stamps;


	bool Ready() const {
		return stamps.Ready();
	}


	std::span<uint32> Span() {
		return std::span<uint32>(stamps.Data(), Slots);
	}


	std::span<const uint32> Span() const {
		return std::span<const uint32>(stamps.Data(), Slots);
	}
};


template<std::size_t Slots, std::size_t Tag, typename Policy>
struct StampStorage<false, Slots, Tag, Policy> {
	constexpr bool Ready() const {
		return true;
	}


	std::span<uint32> Span() {
		return std::span<uint32>();
	}


	std::span<const uint32> Span() const {
		return std::span<const uint32>();
	}
};


// A packet queue of Slots slots of SlotBytes bytes each (Slots - 1 packets are held), or nothing.
template<bool Enabled, std::size_t Slots, std::size_t SlotBytes, std::size_t Tag, typename Policy = Memory::Inside>
struct QueueStorage {
	typename Policy::template Storage<uint8, Slots * SlotBytes> bytes;
	typename Policy::template Storage<uint16, Slots> lengths;
	typename Policy::template Storage<uint8, Slots> flags;
	Indexes indexes;


	bool Ready() const {
		return bytes.Ready() && lengths.Ready() && flags.Ready();
	}


	QueueView View() {
		return QueueView{std::span<uint8>(bytes.Data(), Slots * SlotBytes), std::span<uint16>(lengths.Data(), Slots),
			std::span<uint8>(flags.Data(), Slots), SlotBytes};
	}
};


template<std::size_t Slots, std::size_t SlotBytes, std::size_t Tag, typename Policy>
struct QueueStorage<false, Slots, SlotBytes, Tag, Policy> {
	constexpr bool Ready() const {
		return true;
	}
};

}
