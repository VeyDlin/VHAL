#pragma once
#include <Drivers/Interface/VUB/TestKit/Random.h>
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <span>


// The loss schedule of the link doubles. A double keeps one LinkFaults for each direction it sends in. Each item the direction sends (a
// byte of a stream, a report of HID) gets a fate, in the order the items are sent:
//   - dropped, when its index is a set bit of dropMask (indexes 0 to 63) or the seed's drop chance hits it; a dropped item never reaches
//     the peer, and the sender is told it went out, as a wire does;
//   - flipped, when its index is a set bit of flipMask or the flip chance hits it: one bit of the item is changed, the bit drawn from
//     the seed;
//   - passed otherwise.
// The same plan gives the same fates on every host.


struct LinkFaultPlan {
	uint32 seed = 1;
	uint32 dropPercent = 0;
	uint32 flipPercent = 0;
	uint64 dropMask = 0;
	uint64 flipMask = 0;
};


class LinkFaults {
public:
	enum class Fate : uint8 {
		pass,
		drop,
		flip
	};


	explicit LinkFaults(const LinkFaultPlan& value = LinkFaultPlan()) : plan(value), random(value.seed) {
	}


	// Decides the fate of the next byte of a stream. A flipped byte comes back with one bit changed.
	Fate Byte(uint8& value) {
		std::size_t bit = 0;
		Fate fate = Decide(8, bit);
		if (fate == Fate::flip) {
			value = static_cast<uint8>(value ^ (1u << bit));
		}

		return fate;
	}


	// Decides the fate of the next item of a packet link, such as one HID report. A flipped item has one bit changed, anywhere in it.
	Fate Item(std::span<uint8> item) {
		if (item.empty()) {
			return Fate::pass;
		}

		std::size_t bit = 0;
		Fate fate = Decide(item.size() * 8, bit);
		if (fate == Fate::flip) {
			item[bit / 8] = static_cast<uint8>(item[bit / 8] ^ (1u << (bit % 8)));
		}

		return fate;
	}


	// The items this direction has decided on, and how many of each fate.
	uint32 Items() const {
		return items;
	}


	uint32 Dropped() const {
		return dropped;
	}


	uint32 Flipped() const {
		return flipped;
	}


private:
	// Decides the fate of the next item of bits bits. bit is set to the bit a flip changes.
	Fate Decide(std::size_t bits, std::size_t& bit) {
		uint32 index = items;
		items++;
		bool listedDrop = index < 64 && ((plan.dropMask >> index) & 1u) != 0;
		bool listedFlip = index < 64 && ((plan.flipMask >> index) & 1u) != 0;
		bool chanceDrop = random.Chance(plan.dropPercent);
		bool chanceFlip = random.Chance(plan.flipPercent);
		if (listedDrop || chanceDrop) {
			dropped++;
			return Fate::drop;
		}

		if (listedFlip || chanceFlip) {
			flipped++;
			bit = random.Below(static_cast<uint32>(bits));
			return Fate::flip;
		}

		return Fate::pass;
	}


	LinkFaultPlan plan;
	VUB::Random random;
	uint32 items = 0;
	uint32 dropped = 0;
	uint32 flipped = 0;
};
