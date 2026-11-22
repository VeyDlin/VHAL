#include <Utilities/Math/FFT/FrameCollector.h>
#include <Utilities/Math/FFT/LatestSamples.h>
#include <atomic>
#include <cstdio>
#include <thread>


// Threaded host tests of LatestSamples and FrameCollector. One thread plays the interrupt and pushes the values 1, 2, 3, ...
// in order; the main thread reads. Each value is exact in float (the count stays below 2^24), so a torn or misplaced sample
// shows as a wrong value. The checks:
//   LatestSamples: every sample of a window is a value that was pushed, at most K samples newer than its place in the window
//     when the read began (K is the number of pushes during the read), and never older than that place.
//   FrameCollector: a frame is whole, its samples are consecutive values, and the frames the reader gets never go back.


namespace {


int failureCount = 0;

constexpr uint32 pushCount = 400000;


void Fail(int line, const char *what, double value, double bound) {
	failureCount++;
	std::fprintf(stderr, "TestSourceThreads.cpp:%d: %s %.9g, bound %.9g\n", line, what, value, bound);
}


// One writer pushes pushCount values. The reader reads windows until the writer is done.
template <uint32 Size>
void CheckLatestUnderWrites() {
	LatestSamples<float, Size> latest;
	std::atomic<bool> done{false};
	std::atomic<uint32> started{0};

	std::thread writer([&]() {
		for (uint32 value = 1; value <= pushCount; value++) {
			latest.Push(static_cast<float>(value));
			started.store(value, std::memory_order_release);
		}
		done.store(true, std::memory_order_release);
	});

	uint32 windowsRead = 0;
	uint32 valuesChecked = 0;
	while (!done.load(std::memory_order_acquire)) {
		// Wait until the window is full, so every slot holds a pushed value.
		if (started.load(std::memory_order_acquire) < Size) {
			continue;
		}
		uint32 before = started.load(std::memory_order_acquire);
		for (uint32 index = 0; index < Size; index++) {
			float sample = latest[index];
			double value = static_cast<double>(sample);
			double whole = static_cast<double>(static_cast<uint32>(value));
			if (value != whole) {
				Fail(__LINE__, "torn or fractional sample", value, whole);
			}
			// The place of index in the window when the read began is before - Size + index + 1 (counting pushed values from 1).
			double oldest = static_cast<double>(before) - static_cast<double>(Size) + static_cast<double>(index) + 1.0;
			if (value < oldest) {
				Fail(__LINE__, "sample older than its window place", value, oldest);
			}
			if (value > static_cast<double>(pushCount)) {
				Fail(__LINE__, "sample is not a pushed value", value, static_cast<double>(pushCount));
			}
			valuesChecked++;
		}
		windowsRead++;
	}
	writer.join();

	// After the writer is done, the window is exact again.
	for (uint32 index = 0; index < Size; index++) {
		double expected = static_cast<double>(pushCount) - static_cast<double>(Size) + static_cast<double>(index) + 1.0;
		if (static_cast<double>(latest[index]) != expected) {
			Fail(__LINE__, "final window", static_cast<double>(latest[index]), expected);
		}
	}
	std::printf("LatestSamples %u under writes: %u windows, %u samples checked, final window exact\n",
		static_cast<unsigned>(Size),
		windowsRead,
		valuesChecked
	);
}


// One writer pushes pushCount values into frames of Size with hop Hop. The reader holds each frame it gets while it checks it.
template <uint32 Size, uint32 Hop>
void CheckFramesUnderWrites() {
	FrameCollector<float, Size, Hop> frames;
	std::atomic<bool> done{false};

	std::thread writer([&]() {
		for (uint32 value = 1; value <= pushCount; value++) {
			frames.Push(static_cast<float>(value));
		}
		done.store(true, std::memory_order_release);
	});

	uint32 framesRead = 0;
	uint32 lastFirst = 0;
	uint32 samplesChecked = 0;
	bool finished = false;
	while (!finished) {
		finished = done.load(std::memory_order_acquire);
		typename FrameCollector<float, Size, Hop>::Frame frame = frames.Latest();
		if (!frame.Valid()) {
			continue;
		}
		uint32 first = static_cast<uint32>(static_cast<double>(frame[0]));
		if (first < lastFirst) {
			Fail(__LINE__, "frame goes back in time", first, lastFirst);
		}
		lastFirst = first;
		// The frame is read three times while held, so a write into it during the hold would show.
		for (uint32 pass = 0; pass < 3; pass++) {
			for (uint32 index = 0; index < Size; index++) {
				double expected = static_cast<double>(first) + static_cast<double>(index);
				double value = static_cast<double>(frame[index]);
				if (value != expected) {
					Fail(__LINE__, "frame is not whole or changed while held", value, expected);
				}
				samplesChecked++;
			}
		}
		framesRead++;
	}
	writer.join();

	std::printf("FrameCollector %u hop %u under writes: %u frames read, %u samples checked, every frame whole\n",
		static_cast<unsigned>(Size),
		static_cast<unsigned>(Hop),
		framesRead,
		samplesChecked
	);
	if (framesRead == 0) {
		Fail(__LINE__, "no frame was read", 0.0, 1.0);
	}
}


}


int main() {
	CheckLatestUnderWrites<256>();
	CheckLatestUnderWrites<16>();
	CheckFramesUnderWrites<64, 64>();
	CheckFramesUnderWrites<64, 16>();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestSourceThreads: %d failure(s)\n", failureCount);
		return 1;
	}
	std::printf("TestSourceThreads: all checks passed\n");
	return 0;
}
