#pragma once
#include <Drivers/Interface/VUB/TestKit/Random.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>

namespace VUB {


// The faults a wire applies to each byte or packet it carries. The percentages are chances per byte or per packet. Each item gets
// a delay from 0 to maxDelayUs, so a later item can overtake an earlier one: that is the reordering.
// The outageEveryUs and outageForUs fields give a schedule of outages for a line: the line goes down for outageForUs, then up for a time
// from 1 to outageEveryUs drawn from the seed, and so on. Zero outageEveryUs means the line has no schedule.
struct Faults {
	uint32 seed = 1;
	uint32 dropPercent = 0;
	uint32 corruptPercent = 0;
	uint32 duplicatePercent = 0;
	uint32 maxDelayUs = 0;
	uint32 outageEveryUs = 0;
	uint32 outageForUs = 0;
	// With keepsOrder, an item never becomes due ahead of the item pushed before it: the due time of an item is the later of its own and
	// the last one's. Without it each item is delayed on its own, so a later item can be delivered first.
	bool keepsOrder = false;
};


// What a wire did. Overflow is the wire running out of its own slots, or a packet longer than the wire's packet size: the item is lost on
// the line, and the test kit counts it. Outaged is an item pushed while the wire was down.
struct WireCounts {
	uint32 pushed = 0;
	uint32 dropped = 0;
	uint32 corrupted = 0;
	uint32 duplicated = 0;
	uint32 overflowed = 0;
	uint32 delivered = 0;
	uint32 outaged = 0;
};


// Decides, from the seed, when a line is up and down, for a schedule of outages (see Faults). A line without a schedule is always up.
class OutageSchedule {
public:
	explicit OutageSchedule(const Faults& faults) : everyUs(faults.outageEveryUs), forUs(faults.outageForUs),
		random(faults.seed ^ 0x3C3C3C3Cu), changeUs(0) {
		changeUs = everyUs > 0 ? 1 + random.Below(everyUs) : 0;
	}


	// The state of the line at nowUs: true when it is up. The state changes at the scheduled time, and the next change is drawn then.
	bool UpAt(uint32 nowUs) {
		if (everyUs == 0) {
			return true;
		}

		if (Due(nowUs, changeUs)) {
			up = !up;
			changeUs = nowUs + (up ? 1 + random.Below(everyUs) : forUs);
		}

		return up;
	}


private:
	uint32 everyUs;
	uint32 forUs;
	Random random;
	uint32 changeUs;
	bool up = true;
};


// The due time of an item pushed now with the delay drawn for it. With keepsOrder the due time is the later of that and the last item's, so
// an item is never delivered before the item pushed before it. Times are compared as differences, so the 32-bit wrap does not change the order.
inline uint32 KeptDueUs(const Faults& faults, bool& hasLast, uint32& lastDueUs, uint32 dueUs) {
	if (faults.keepsOrder) {
		if (hasLast && static_cast<int32>(lastDueUs - dueUs) > 0) {
			dueUs = lastDueUs;
		}

		lastDueUs = dueUs;
		hasLast = true;
	}

	return dueUs;
}


// One direction of a byte-stream line. Push takes the bytes a sender wrote; Deliver hands the bytes that are due at the wire's time to
// a sink with Feed(uint8), earliest first. Each byte is dropped, corrupted (one bit flipped), duplicated or delayed by the faults.
template<std::size_t Capacity>
class StreamWire {
public:
	StreamWire(const Faults& faultSet, uint32 seed) : faults(faultSet), random(seed) {
	}


	void SetNow(uint32 nowUs) {
		now = nowUs;
	}


	// Takes the line down or up. A byte pushed while the line is down is lost, and counted as outaged.
	void SetDown(bool value) {
		down = value;
	}


	void Push(std::span<const uint8> bytes) {
		for (uint8 byte : bytes) {
			counts.pushed++;
			if (down) {
				counts.outaged++;
				continue;
			}

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
				if (!items[index].used || static_cast<int32>(items[index].dueUs - now) > 0) {
					continue;
				}

				if (best == Capacity || static_cast<int32>(items[index].dueUs - items[best].dueUs) < 0 ||
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


	WireCounts Counts() const {
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
				uint32 dueUs = KeptDueUs(faults, hasLast, lastDueUs, now + random.Below(faults.maxDelayUs + 1));
				items[index] = Item{value, dueUs, nextSequence, true};
				nextSequence++;
				return;
			}
		}

		counts.overflowed++;
	}


	Faults faults;
	Random random;
	std::array<Item, Capacity> items{};
	uint32 now = 0;
	uint32 nextSequence = 0;
	bool hasLast = false;
	uint32 lastDueUs = 0;
	WireCounts counts;
	bool down = false;
};


// One direction of a packet line. Each packet is kept whole. Corruption flips one bit of one byte. A packet longer than PacketBytes is
// not stored: it is counted as overflowed. Nothing is ever cut short.
template<std::size_t Capacity, std::size_t PacketBytes>
class PacketWire {
public:
	PacketWire(const Faults& faultSet, uint32 seed) : faults(faultSet), random(seed) {
	}


	void SetNow(uint32 nowUs) {
		now = nowUs;
	}


	// Takes the line down or up. A packet pushed while the line is down is lost, and counted as outaged.
	void SetDown(bool value) {
		down = value;
	}


	void Push(std::span<const uint8> packet) {
		counts.pushed++;
		if (down) {
			counts.outaged++;
			return;
		}

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
				if (!items[index].used || static_cast<int32>(items[index].dueUs - now) > 0) {
					continue;
				}

				if (best == Capacity || static_cast<int32>(items[index].dueUs - items[best].dueUs) < 0 ||
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


	WireCounts Counts() const {
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

			item.dueUs = KeptDueUs(faults, hasLast, lastDueUs, now + random.Below(faults.maxDelayUs + 1));
			item.sequence = nextSequence;
			item.used = true;
			nextSequence++;
			return;
		}

		counts.overflowed++;
	}


	Faults faults;
	Random random;
	std::array<Item, Capacity> items{};
	uint32 now = 0;
	uint32 nextSequence = 0;
	bool hasLast = false;
	uint32 lastDueUs = 0;
	WireCounts counts;
	bool down = false;
};

}
