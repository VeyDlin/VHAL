#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <System/System.h>
#include <array>
#include <cstddef>
#include <span>


namespace VUB {


// Holds the critical section of a concurrent requests layer for the life of the object. The layer's tables are shared with the tasks that call
// it, so each access to them is made inside one. Process calls the layer's handlers outside every section. A layer that is not concurrent has
// an empty object, and no call to System::CriticalSection is built.
template<bool Enabled>
class RequestSection {
public:
	RequestSection() {
		if constexpr (Enabled) {
			System::CriticalSection(true);
		}
	}


	~RequestSection() {
		if constexpr (Enabled) {
			System::CriticalSection(false);
		}
	}


	RequestSection(const RequestSection&) = delete;
	RequestSection& operator=(const RequestSection&) = delete;
};


// One packet of the outgoing queue: its bytes, the length of the packet and the flags of its Meta.
template<std::size_t Bytes>
struct OutgoingPacket {
	std::array<uint8, Bytes> bytes{};
	uint16 length = 0;
	uint8 flags = 0;
};


// The packets that the requests, the cancels, the fires, the pings and the events of a concurrent layer wait to send, oldest first. Another task
// adds a packet inside a section; Process sends the front one outside every section and removes it inside one. Only Process removes a packet, and
// an entry is written only while it is free, so the front packet is read without a lock. A full queue refuses the packet: the caller sends it again.
template<std::size_t Bytes, std::size_t Count>
class OutgoingQueue {
	static_assert(Count > 0 && Count <= 255, "Pending needs from 1 to 255 packets");
public:
	bool Empty() const {
		return used == 0;
	}


	bool Full() const {
		return used == Count;
	}


	// The front packet. It is valid while the queue is not empty, and until Pop.
	const OutgoingPacket<Bytes>& Front() const {
		return entries[head];
	}


	// Adds a packet at the back. The caller has checked that the queue is not full, inside the same section.
	void Push(std::span<const uint8> packet, uint8 flags) {
		std::size_t tail = (static_cast<std::size_t>(head) + used) % Count;
		OutgoingPacket<Bytes>& entry = entries[tail];
		for (std::size_t index = 0; index < packet.size(); index++) {
			entry.bytes[index] = packet[index];
		}

		entry.length = static_cast<uint16>(packet.size());
		entry.flags = flags;
		used++;
	}


	// Removes the front packet, after it was sent or refused for good.
	void Pop() {
		head = static_cast<uint8>((head + 1) % Count);
		used--;
	}


private:
	std::array<OutgoingPacket<Bytes>, Count> entries{};
	uint8 head = 0;
	uint8 used = 0;
};


// The waiter of an Await. It lives on the stack of the task that waits, and the slot of its request points at it. The layer's Process writes the
// final into it with deliver, inside a critical section, and sets done; an abandoned wait clears the slot's pointer inside a section, so no write
// after the abandon reaches the task's stack. The typed final is in the derived waiter (AwaitWaiterOf).
struct AwaitWaiter {
	using DeliverFn = void (*)(AwaitWaiter& waiter, ResultStatus result, uint8 status, std::span<const uint8> body);


	DeliverFn deliver = nullptr;
	bool done = false;
	ResultStatus result = ResultStatus::ok;
};


// The waiter of a command with a reply: the typed final that deliver fills.
template<typename FinalType>
struct AwaitWaiterOf : AwaitWaiter {
	FinalType value{};
};


// The pointer a slot of an awaited request keeps. A slot of a layer that is not awaitable has none.
struct AwaitSlot {
	AwaitWaiter* waiter = nullptr;
};


// The stand-ins of the concurrent parts for a layer that is not concurrent. Each is its own type: several empty members of one type each take
// a byte, while members of different empty types can share an address, so a part that is off costs no bytes.
struct NoAwaitSlot {
};


struct NoOutgoing {
};


}
