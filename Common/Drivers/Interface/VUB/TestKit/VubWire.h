#pragma once
#include <Drivers/Interface/VUB/TestKit/VubRandom.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>


// The faults a wire applies to each byte or packet it carries. The percentages are chances per byte or per packet. Each item gets
// a delay from 0 to maxDelayUs, so a later item can overtake an earlier one: that is the reordering.
struct VubFaults {
	uint32 seed = 1;
	uint32 dropPercent = 0;
	uint32 corruptPercent = 0;
	uint32 duplicatePercent = 0;
	uint32 maxDelayUs = 0;
};


// What a wire did. Overflow is the wire running out of its own slots, or a packet longer than the wire's packet size: the item is lost on
// the line, and the test kit counts it.
struct VubWireCounts {
	uint32 pushed = 0;
	uint32 dropped = 0;
	uint32 corrupted = 0;
	uint32 duplicated = 0;
	uint32 overflowed = 0;
	uint32 delivered = 0;
};


// One direction of a byte-stream line. Push takes the bytes a sender wrote; Deliver hands the bytes that are due at the wire's time to
// a sink with Feed(uint8), earliest first. Each byte is dropped, corrupted (one bit flipped), duplicated or delayed by the faults.
template<std::size_t Capacity>
class VubStreamWire {
public:
	VubStreamWire(const VubFaults& faultSet, uint32 seed) : faults(faultSet), random(seed) {
	}


	void SetNow(uint32 nowUs) {
		now = nowUs;
	}


	void Push(std::span<const uint8> bytes) {
		for (uint8 byte : bytes) {
			counts.pushed++;
			if (random.Chance(faults.dropPercent)) {
				counts.dropped++;
				continue;
			}

			uint8 value = byte;
			if (random.Chance(faults.corruptPercent)) {
				value = static_cast<uint8>(value ^ (1u << random.Below(8)));
				counts.corrupted++;
			}

			Store(value);
			if (random.Chance(faults.duplicatePercent)) {
				counts.duplicated++;
				Store(value);
			}
		}
	}


	template<typename Sink>
	void Deliver(Sink& sink) {
		while (true) {
			std::size_t best = Capacity;
			for (std::size_t index = 0; index < Capacity; index++) {
				if (!items[index].used || items[index].dueUs > now) {
					continue;
				}

				if (best == Capacity || items[index].dueUs < items[best].dueUs ||
					(items[index].dueUs == items[best].dueUs && items[index].sequence < items[best].sequence)) {
					best = index;
				}
			}

			if (best == Capacity) {
				return;
			}

			items[best].used = false;
			counts.delivered++;
			sink.Feed(items[best].value);
		}
	}


	VubWireCounts Counts() const {
		return counts;
	}


private:
	struct Item {
		uint8 value;
		uint32 dueUs;
		uint32 sequence;
		bool used;
	};


	void Store(uint8 value) {
		for (std::size_t index = 0; index < Capacity; index++) {
			if (!items[index].used) {
				items[index] = Item{value, now + random.Below(faults.maxDelayUs + 1), nextSequence, true};
				nextSequence++;
				return;
			}
		}

		counts.overflowed++;
	}


	VubFaults faults;
	VubRandom random;
	std::array<Item, Capacity> items{};
	uint32 now = 0;
	uint32 nextSequence = 0;
	VubWireCounts counts;
};


// One direction of a packet line. Each packet is kept whole. Corruption flips one bit of one byte. A packet longer than PacketBytes is
// not stored: it is counted as overflowed. Nothing is ever cut short.
template<std::size_t Capacity, std::size_t PacketBytes>
class VubPacketWire {
public:
	VubPacketWire(const VubFaults& faultSet, uint32 seed) : faults(faultSet), random(seed) {
	}


	void SetNow(uint32 nowUs) {
		now = nowUs;
	}


	void Push(std::span<const uint8> packet) {
		counts.pushed++;
		if (random.Chance(faults.dropPercent)) {
			counts.dropped++;
			return;
		}

		Store(packet);
		if (random.Chance(faults.duplicatePercent)) {
			counts.duplicated++;
			Store(packet);
		}
	}


	template<typename Sink>
	void Deliver(Sink& sink) {
		while (true) {
			std::size_t best = Capacity;
			for (std::size_t index = 0; index < Capacity; index++) {
				if (!items[index].used || items[index].dueUs > now) {
					continue;
				}

				if (best == Capacity || items[index].dueUs < items[best].dueUs ||
					(items[index].dueUs == items[best].dueUs && items[index].sequence < items[best].sequence)) {
					best = index;
				}
			}

			if (best == Capacity) {
				return;
			}

			items[best].used = false;
			counts.delivered++;
			sink.FeedPacket(std::span<const uint8>(items[best].bytes.data(), items[best].length));
		}
	}


	VubWireCounts Counts() const {
		return counts;
	}


private:
	struct Item {
		std::array<uint8, PacketBytes> bytes;
		std::size_t length;
		uint32 dueUs;
		uint32 sequence;
		bool used;
	};


	void Store(std::span<const uint8> packet) {
		if (packet.size() > PacketBytes) {
			counts.overflowed++;
			return;
		}

		for (std::size_t index = 0; index < Capacity; index++) {
			if (items[index].used) {
				continue;
			}

			Item& item = items[index];
			item.length = packet.size();
			for (std::size_t byteIndex = 0; byteIndex < item.length; byteIndex++) {
				item.bytes[byteIndex] = packet[byteIndex];
			}

			if (item.length > 0 && random.Chance(faults.corruptPercent)) {
				std::size_t target = random.Below(static_cast<uint32>(item.length));
				item.bytes[target] = static_cast<uint8>(item.bytes[target] ^ (1u << random.Below(8)));
				counts.corrupted++;
			}

			item.dueUs = now + random.Below(faults.maxDelayUs + 1);
			item.sequence = nextSequence;
			item.used = true;
			nextSequence++;
			return;
		}

		counts.overflowed++;
	}


	VubFaults faults;
	VubRandom random;
	std::array<Item, Capacity> items{};
	uint32 now = 0;
	uint32 nextSequence = 0;
	VubWireCounts counts;
};
