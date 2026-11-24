#pragma once
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>


namespace DemoVub {
// Keeps copies of the payloads a stack received until the stack takes them back to send. The receive handler runs inside the stack's
// Process, and the queue is read in the main loop, so the queue is used from one context and needs no volatile.
template<std::size_t PayloadBytes, std::size_t Slots>
class EchoQueue {
	static_assert(PayloadBytes > 0, "an echo slot holds at least one byte");
	static_assert(Slots > 0, "an echo queue has at least one slot");


public:
	// Stores a copy of the payload. Returns false when every slot is taken; the payload is then not stored.
	bool Push(std::span<const uint8> payload) {
		if (count == Slots || payload.size() > PayloadBytes) {
			return false;
		}

		std::size_t tail = (head + count) % Slots;
		lengths[tail] = payload.size();
		for (std::size_t index = 0; index < payload.size(); index++) {
			slots[tail][index] = payload[index];
		}

		count++;
		return true;
	}


	// The oldest stored payload. Valid while Count() is above zero.
	std::span<const uint8> Front() const {
		return std::span<const uint8>(slots[head].data(), lengths[head]);
	}


	// Removes the oldest payload.
	void Pop() {
		head = (head + 1) % Slots;
		count--;
	}


	std::size_t Count() const {
		return count;
	}


private:
	std::array<std::array<uint8, PayloadBytes>, Slots> slots{};
	std::array<std::size_t, Slots> lengths{};
	std::size_t head = 0;
	std::size_t count = 0;
};
}
