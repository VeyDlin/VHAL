#pragma once
#include <array>
#include <cstddef>
#include <span>


// A list with a fixed capacity and no heap. The host tests record from interrupt-context callbacks, so these lists never grow.
// An item that does not fit is dropped and sets overflowed; the tests check that flag, so a dropped record cannot pass as a
// complete one.
template<typename TItem, std::size_t Capacity>
struct FixedList {
	std::array<TItem, Capacity> items{};
	std::size_t count = 0;
	bool overflowed = false;


	void Push(const TItem& item) {
		if (count == Capacity) {
			overflowed = true;
			return;
		}

		items[count] = item;
		count++;
	}


	// Empties the list. The overflow flag stays set, so a test sees that a record was lost at any point.
	void Clear() {
		count = 0;
	}


	// The last item. The list must not be empty; for an empty list this returns the first slot, which a test then fails on.
	const TItem& Back() const {
		return items[count > 0 ? count - 1 : 0];
	}


	const TItem& operator[](std::size_t index) const {
		return items[index];
	}


	std::span<const TItem> View() const {
		return std::span<const TItem>(items.data(), count);
	}
};
