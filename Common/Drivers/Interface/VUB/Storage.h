#pragma once
#include <Drivers/Interface/VUB/Ring.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>

namespace VUB {


// Storage that exists only when the stack has a use for it. A disabled storage has no members, so a member of that type declared
// [[no_unique_address]] takes no RAM. Tag gives each storage its own type, so two disabled ones may share an address. The bytes of a buffer
// are not set when the stack is built: they are written ahead of each read, and the indexes say which of them are.


// A byte buffer of Bytes bytes, or nothing.
template<bool Enabled, std::size_t Bytes, std::size_t Tag>
struct Buffer {
	std::array<uint8, Bytes> bytes;


	std::span<uint8> Span() {
		return std::span<uint8>(bytes);
	}


	std::span<const uint8> Span() const {
		return std::span<const uint8>(bytes);
	}
};


template<std::size_t Bytes, std::size_t Tag>
struct Buffer<false, Bytes, Tag> {
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
template<bool Enabled, std::size_t Slots, std::size_t Tag>
struct RingStorage {
	std::array<uint8, Slots> slots;
	Indexes indexes;


	std::span<uint8> Span() {
		return std::span<uint8>(slots);
	}
};


template<std::size_t Slots, std::size_t Tag>
struct RingStorage<false, Slots, Tag> {
};


// The receive time of each byte of a ring of Slots slots, or nothing. Only a chain whose framing uses time has it.
template<bool Enabled, std::size_t Slots, std::size_t Tag>
struct StampStorage {
	std::array<uint32, Slots> stamps;


	std::span<uint32> Span() {
		return std::span<uint32>(stamps);
	}


	std::span<const uint32> Span() const {
		return std::span<const uint32>(stamps);
	}
};


template<std::size_t Slots, std::size_t Tag>
struct StampStorage<false, Slots, Tag> {
	std::span<uint32> Span() {
		return std::span<uint32>();
	}


	std::span<const uint32> Span() const {
		return std::span<const uint32>();
	}
};


// A packet queue of Slots slots of SlotBytes bytes each (Slots - 1 packets are held), or nothing.
template<bool Enabled, std::size_t Slots, std::size_t SlotBytes, std::size_t Tag>
struct QueueStorage {
	std::array<uint8, Slots * SlotBytes> bytes;
	std::array<uint16, Slots> lengths;
	std::array<uint8, Slots> flags;
	Indexes indexes;


	QueueView View() {
		return QueueView{std::span<uint8>(bytes), std::span<uint16>(lengths), std::span<uint8>(flags), SlotBytes};
	}
};


template<std::size_t Slots, std::size_t SlotBytes, std::size_t Tag>
struct QueueStorage<false, Slots, SlotBytes, Tag> {
};

}
