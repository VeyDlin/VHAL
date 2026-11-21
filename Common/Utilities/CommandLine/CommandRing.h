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


// The input ring of a console. The producer is Feed, which may run in an interrupt. The consumer is Process, which runs in the main loop.
//
// Every counter has one writer, so each side uses only loads and stores. That matters on Cortex-M0, which has no atomic
// read-modify-write instructions. The producer owns written, lost, the refused Ctrl+C count and the places it records. The consumer owns
// consumed and the count of presses it has taken. No side masks interrupts, so an interrupt never blocks.
//
// Each refused Ctrl+C has its own place: the number of bytes accepted before it. Two slots hold the places of the presses that wait.
// At most two wait at once, and a press refused while two wait is dropped. Press n uses slot n % 2. The producer writes a slot only
// while fewer than two presses wait. The consumer reads a slot only while its press waits. So no slot is read and written at once.
//
// interruptCount (producer, release) publishes a place. interruptTaken (consumer, release) frees a slot. The producer reads
// interruptTaken with acquire, so the consumer's last read of a slot comes before the producer's next write to it. A stale read of
// the taken count only makes the producer drop a press. It never gives a press a wrong place.
//
// Pop does not take a byte that lies at a due place, so the Interrupt comes before that byte. The producer publishes a place before it
// stores the byte at that place. So when Pop sees the byte, it also sees the press.
//
// The positions and the lost count are uint32 on every target, so they wrap at 2^32 on the host too. The index of a byte is its position
// modulo the size. Only a power-of-two size divides 2^32, so each slot keeps its index across the wrap. Positions are compared only
// through Reached, which compares them modulo 2^32. The ring holds at most 2^30 bytes, so the signed difference of two positions is
// exact: it is at most 2^30 in size, and a difference of 2^31 would read as negative.
//
// RingBuffer.h does not fit. Each of its operations masks interrupts, which Feed must not do from an interrupt. It also holds a
// std::function member, which the console does not use.
template<std::size_t Size>
class CommandRing {
	static_assert(Size > 0, "a command line input ring needs at least one byte");
	static_assert((Size & (Size - 1)) == 0, "a command line input ring needs a power-of-two size");
	static_assert(Size <= 1073741824u, "a command line input ring holds at most 2^30 bytes");

private:
	uint8 bytes[Size]{};
	std::atomic<uint32> written{0};
	std::atomic<uint32> consumed{0};
	std::atomic<uint32> interruptCount{0};
	std::atomic<uint32> interruptTaken{0};
	uint32 interruptAt[2]{};
	std::atomic<uint32> lostTotal{0};
	uint32 lostSeen = 0;


public:
	// Sets every position to start. The tests start near 2^32, so that they cross the wrap. The console keeps the default, 0.
	explicit CommandRing(uint32 start = 0)
		: written{start}
		, consumed{start}
		, interruptAt{start, start} {
	}


	// Stores the byte. Returns false, and counts the byte as lost, when the ring is full.
	bool Push(uint8 byte) {
		uint32 put = written.load(std::memory_order_relaxed);

		if (put - consumed.load(std::memory_order_acquire) >= Size) {
			lostTotal.store(lostTotal.load(std::memory_order_relaxed) + 1, std::memory_order_release);
			return false;
		}

		bytes[put % Size] = byte;
		written.store(put + 1, std::memory_order_release);
		return true;
	}


	// Records a refused Ctrl+C. Its place is the bytes accepted so far, so the consumer runs them first. Returns false, and drops the press,
	// when two presses already wait. Feed ignores the result.
	bool RefuseInterrupt() {
		uint32 refused = interruptCount.load(std::memory_order_relaxed);

		if (refused - interruptTaken.load(std::memory_order_acquire) >= 2) {
			return false;
		}

		interruptAt[refused % 2] = written.load(std::memory_order_relaxed);
		interruptCount.store(refused + 1, std::memory_order_release);
		return true;
	}


	// Takes the oldest byte. Returns false when the ring is empty. It also returns false when the byte lies at the place of a due refused
	// Ctrl+C. The consumer then calls InterruptDue, which takes that press before the byte.
	bool Pop(uint8& byte) {
		uint32 taken = consumed.load(std::memory_order_relaxed);

		if (written.load(std::memory_order_acquire) == taken) {
			return false;
		}

		// The count is read after written, so a press published before the byte at its place is seen here.
		uint32 next = interruptTaken.load(std::memory_order_relaxed);

		if (interruptCount.load(std::memory_order_acquire) != next && Reached(taken, interruptAt[next % 2])) {
			return false;
		}

		byte = bytes[taken % Size];
		consumed.store(taken + 1, std::memory_order_release);
		return true;
	}


	// True once for each refused Ctrl+C whose place the consumer has reached, oldest first. Call it before each Pop, and again after a Pop
	// that returns false.
	bool InterruptDue() {
		uint32 next = interruptTaken.load(std::memory_order_relaxed);

		if (interruptCount.load(std::memory_order_acquire) == next) {
			return false;
		}

		if (!Reached(consumed.load(std::memory_order_relaxed), interruptAt[next % 2])) {
			return false;
		}

		interruptTaken.store(next + 1, std::memory_order_release);
		return true;
	}


	// True while a refused Ctrl+C waits that the consumer has not taken. A long command checks it on each pass, because the consumer
	// does not reach the place of a refused Ctrl+C while the command runs.
	bool InterruptWaiting() {
		return interruptCount.load(std::memory_order_acquire) != interruptTaken.load(std::memory_order_relaxed);
	}


	// Takes the oldest waiting refused Ctrl+C, and drops the queued bytes before its place, so the cancel does not run them. The bytes after
	// the place stay, and a later press keeps its own place. Call it only after InterruptWaiting returned true.
	void TakeWaitingInterrupt() {
		uint32 next = interruptTaken.load(std::memory_order_relaxed);
		uint32 place = interruptAt[next % 2];

		if (Reached(place, consumed.load(std::memory_order_relaxed))) {
			consumed.store(place, std::memory_order_release);
		}

		interruptTaken.store(next + 1, std::memory_order_release);
	}


	// Drops the bytes up to and including the first one equal to byte, and returns true. Returns false, and drops nothing, when there is no
	// such byte before the place of the oldest waiting refused Ctrl+C. The consumer uses it to cancel a command at a Ctrl+C that waits
	// behind other input. The search stops at that place, so a later byte is not dropped here: that press keeps its own Interrupt.
	bool DiscardTo(uint8 byte) {
		uint32 taken = consumed.load(std::memory_order_relaxed);
		uint32 end = written.load(std::memory_order_acquire);
		uint32 next = interruptTaken.load(std::memory_order_relaxed);

		if (interruptCount.load(std::memory_order_acquire) != next && Reached(end, interruptAt[next % 2])) {
			end = interruptAt[next % 2];
		}

		uint32 queued = end - taken;

		for (uint32 offset = 0; offset < queued; offset++) {
			if (bytes[(taken + offset) % Size] == byte) {
				consumed.store(taken + offset + 1, std::memory_order_release);
				return true;
			}
		}

		return false;
	}


	// The number of bytes lost since the last call, which the consumer takes.
	uint32 TakeLost() {
		uint32 total = lostTotal.load(std::memory_order_acquire);
		uint32 lost = total - lostSeen;
		lostSeen = total;
		return lost;
	}


private:
	// True when position is at or past place. The positions wrap, so the order comes from their signed difference. Two positions of the
	// ring are at most Size apart, and Size is at most 2^30, so that difference is exact.
	static bool Reached(uint32 position, uint32 place) {
		return static_cast<int32>(position - place) >= 0;
	}
};
