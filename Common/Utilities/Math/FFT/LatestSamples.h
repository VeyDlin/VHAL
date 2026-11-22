#pragma once
#include <Utilities/DataTypes.h>
#include <atomic>
#include <type_traits>


// A ring of the newest Size samples, fed from an interrupt. Push adds a sample as the newest, and the FFT reads the window as
// a sample source: sample 0 is the oldest of the newest Size samples and sample Size - 1 the newest. A sample not yet pushed
// reads as zero.
//
// One writer (the interrupt) and one reader (the main loop). Push writes a slot and then publishes the count with a release
// store; it does no read-modify-write, so it needs no atomic instruction, and the one counter has one writer. The reader
// loads the count with an acquire load for every sample, so it never reads a slot before its sample is complete. The slots
// are accessed with relaxed atomic loads and stores, so the interrupt and the reader never make a plain data race on one.
//
// The reader takes the count afresh for each sample, so a Push that lands during an Analyze can make the window mixed. Each
// sample is still a whole value, and it is at most K samples newer than its place in the window when the load began, where K
// is the number of pushes during the load. Neighbours can then repeat or skip across the seam, so the window is not exact.
// To read an exact window, mask the interrupt for the length of the Analyze call.
template <typename Number, uint32 Size>
class LatestSamples {
	static_assert(Size >= 2 && (Size & (Size - 1)) == 0, "LatestSamples: Size must be a power of two, at least 2");
	static_assert(sizeof(Number) <= 4, "LatestSamples: Number must fit one 32-bit word, so a store is one bus access");

public:
	static constexpr uint32 size = Size;


private:
	static constexpr uint32 mask = Size - 1;

	Number ring[Size] = {};
	// Writer only: the number of samples pushed, modulo 2^32. Size divides 2^32, so the slot of a count is count & mask.
	uint32 total = 0;
	// Written by Push, read by the reader.
	std::atomic<uint32> published{0};


public:
	// Called from the interrupt, once per sample.
	void Push(Number sample) {
		std::atomic_ref<Number>(ring[total & mask]).store(sample, std::memory_order_relaxed);
		total = total + 1;
		published.store(total, std::memory_order_release);
	}


	// Sample i of the window, where 0 is the oldest and Size - 1 the newest. The window is the newest Size samples as of the
	// count read for this sample.
	Number operator[](uint32 index) const {
		uint32 count = published.load(std::memory_order_acquire);
		return std::atomic_ref<const Number>(ring[(count + index) & mask]).load(std::memory_order_relaxed);
	}
};
