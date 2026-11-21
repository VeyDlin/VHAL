#pragma once
#include <Utilities/DataTypes.h>
#include <atomic>
#include <cstddef>


// The size of a console's input ring, listed among its types: CommandLine<Set, App, InputRing<64>, Help>. Without it the ring is 32
// bytes. A console lists at most one.
template<std::size_t Size>
struct InputRing {
	static_assert(Size > 0, "a command line input ring needs at least one byte");
};


template<typename Type>
inline constexpr bool CommandIsInputRing = false;


template<std::size_t Size>
inline constexpr bool CommandIsInputRing<InputRing<Size>> = true;


// The ring size of a console: the size of its InputRing, or 32 bytes when it lists none.
template<typename... Arguments>
struct CommandRingSizeOf {
	static constexpr std::size_t Value = 32;
};


template<std::size_t Size, typename... Rest>
struct CommandRingSizeOf<InputRing<Size>, Rest...> {
	static constexpr std::size_t Value = Size;
};


template<typename First, typename... Rest>
struct CommandRingSizeOf<First, Rest...> : CommandRingSizeOf<Rest...> {
};


// The input ring of a console: bytes from one producer, which is Feed and may run in an interrupt, to one consumer, which is Process in
// the main loop. Every counter has one writer, so each side uses only loads and stores and never a read-modify-write. That matters on
// Cortex-M0, which has no atomic read-modify-write instructions. The producer owns written, lost and the refused Ctrl+C; the consumer
// owns consumed. No side masks interrupts, so an interrupt never blocks.
//
// RingBuffer.h does not fit: each of its operations masks interrupts, which Feed must not do from an interrupt, and it holds a
// std::function member, which the console does not use.
template<std::size_t Size>
class CommandRing {
	static_assert(Size > 0, "a command line input ring needs at least one byte");

private:
	uint8 bytes[Size]{};
	std::atomic<std::size_t> written{0};
	std::atomic<std::size_t> consumed{0};
	std::atomic<std::size_t> interruptAt{0};
	std::atomic<uint32> interruptCount{0};
	std::atomic<uint32> lostTotal{0};
	uint32 interruptDone = 0;
	uint32 lostSeen = 0;


public:
	// Stores the byte. Returns false, and counts the byte as lost, when the ring is full.
	bool Push(uint8 byte) {
		std::size_t put = written.load(std::memory_order_relaxed);

		if (put - consumed.load(std::memory_order_acquire) >= Size) {
			lostTotal.store(lostTotal.load(std::memory_order_relaxed) + 1, std::memory_order_release);
			return false;
		}

		bytes[put % Size] = byte;
		written.store(put + 1, std::memory_order_release);
		return true;
	}


	// Records a Ctrl+C that was refused. It takes place after the bytes accepted so far, so the consumer runs them first. A refused
	// Ctrl+C that comes while an earlier one is still waiting moves the place to its own, the later one.
	void RefuseInterrupt() {
		interruptAt.store(written.load(std::memory_order_relaxed), std::memory_order_relaxed);
		interruptCount.store(interruptCount.load(std::memory_order_relaxed) + 1, std::memory_order_release);
	}


	// Takes the oldest byte. Returns false when the ring is empty.
	bool Pop(uint8& byte) {
		std::size_t taken = consumed.load(std::memory_order_relaxed);

		if (written.load(std::memory_order_acquire) == taken) {
			return false;
		}

		byte = bytes[taken % Size];
		consumed.store(taken + 1, std::memory_order_release);
		return true;
	}


	// True once for each refused Ctrl+C whose place the consumer has reached. Call it before each Pop.
	bool InterruptDue() {
		uint32 requested = interruptCount.load(std::memory_order_acquire);

		if (requested == interruptDone || consumed.load(std::memory_order_relaxed) < interruptAt.load(std::memory_order_relaxed)) {
			return false;
		}

		interruptDone = requested;
		return true;
	}


	// The number of bytes lost since the last call, which the consumer takes.
	uint32 TakeLost() {
		uint32 total = lostTotal.load(std::memory_order_acquire);
		uint32 lost = total - lostSeen;
		lostSeen = total;
		return lost;
	}
};
