#pragma once
#include <Drivers/Interface/VUB/VubRing.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>


// Storage that exists only when the stack has a use for it. A disabled storage has no members, so a member of that type declared
// [[no_unique_address]] takes no RAM. Tag gives each storage its own type, so two disabled ones may share an address.


// A byte buffer of Bytes bytes, or nothing.
template<bool Enabled, std::size_t Bytes, std::size_t Tag>
struct VubBuffer {
	std::array<uint8, Bytes> bytes{};


	std::span<uint8> Span() {
		return std::span<uint8>(bytes);
	}


	std::span<const uint8> Span() const {
		return std::span<const uint8>(bytes);
	}
};


template<std::size_t Bytes, std::size_t Tag>
struct VubBuffer<false, Bytes, Tag> {
	std::span<uint8> Span() {
		return std::span<uint8>();
	}


	std::span<const uint8> Span() const {
		return std::span<const uint8>();
	}
};


// A value of type T, or nothing.
template<bool Enabled, typename T, std::size_t Tag>
struct VubState {
	T value{};


	T& Get() {
		return value;
	}


	const T& Get() const {
		return value;
	}
};


template<typename T, std::size_t Tag>
struct VubState<false, T, Tag> {
};


// A byte ring of Slots slots (Slots - 1 bytes are held), or nothing.
template<bool Enabled, std::size_t Slots, std::size_t Tag>
struct VubRingStorage {
	std::array<uint8, Slots> slots{};
	VubIndexes indexes;


	std::span<uint8> Span() {
		return std::span<uint8>(slots);
	}
};


template<std::size_t Slots, std::size_t Tag>
struct VubRingStorage<false, Slots, Tag> {
};


// A packet queue of Slots slots of SlotBytes bytes each (Slots - 1 packets are held), or nothing.
template<bool Enabled, std::size_t Slots, std::size_t SlotBytes, std::size_t Tag>
struct VubQueueStorage {
	std::array<uint8, Slots * SlotBytes> bytes{};
	std::array<uint16, Slots> lengths{};
	std::array<uint8, Slots> flags{};
	VubIndexes indexes;


	VubQueueView View() {
		return VubQueueView{std::span<uint8>(bytes), std::span<uint16>(lengths), std::span<uint8>(flags), SlotBytes};
	}
};


template<std::size_t Slots, std::size_t SlotBytes, std::size_t Tag>
struct VubQueueStorage<false, Slots, SlotBytes, Tag> {
};
