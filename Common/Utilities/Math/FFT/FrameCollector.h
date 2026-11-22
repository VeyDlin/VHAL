#pragma once
#include <Utilities/DataTypes.h>
#include <atomic>


// Collects frames of Size samples from an interrupt, and hands the newest complete frame to the reader as a sample source.
// Push adds a sample from the interrupt. Two buffers take turns: the writer fills one while the reader may read the other.
//
// Frames overlap when Hop is smaller than Size: each new frame keeps the last Size - Hop samples of the previous one and adds
// Hop new samples, so a frame is published every Hop samples. The overlap is kept by copying those samples from the published
// buffer into the buffer being filled, once per frame, inside Push. With Hop equal to Size the frames do not overlap and
// nothing is copied.
//
// The reader holds at most one frame at a time. Latest leases the buffer of the newest frame, and the lease ends when the
// Frame object is destroyed. The writer never writes a leased buffer: if the reader still holds the buffer the next frame
// needs, the writer drops that frame, counts nothing, and resumes with a frame that starts without overlap. A frame the
// reader gets is always whole and stable, and it is never mixed with samples of a later frame.
//
// Ordering: each shared word has one writer. The writer announces the buffer it fills (writingTarget) before it checks the
// lease, and the reader announces its lease before it checks writingTarget, with a sequentially consistent fence between
// the two steps on each side. Whichever side comes second sees the other, so the reader and the writer never share a buffer.
//
// The shared words store the buffer index plus one, so 0 means no buffer. An all-zero collector is its initial state, which
// keeps it in zero-initialized memory rather than in an initialized data image.
template <typename Number, uint32 Size, uint32 Hop = Size>
class FrameCollector {
	static_assert(Size >= 1, "FrameCollector: Size must be at least 1");
	static_assert(Hop >= 1 && Hop <= Size, "FrameCollector: Hop must be in 1..Size");
	static_assert(sizeof(Number) <= 4, "FrameCollector: Number must fit one 32-bit word, so a store is one bus access");

public:
	static constexpr uint32 size = Size;
	static constexpr uint32 hop = Hop;

	// A stable view of one frame. Destroying it releases the buffer. An invalid frame (no complete frame yet, the writer
	// busy with the buffer, or a Latest call while a frame is already held) reads as zeros.
	class Frame {
	public:
		Frame(const Frame &other) = delete;
		Frame &operator=(const Frame &other) = delete;
		Frame(Frame &&other) = delete;
		Frame &operator=(Frame &&other) = delete;

		~Frame() {
			if (owner != nullptr) {
				owner->Release();
			}
		}


		bool Valid() const {
			return owner != nullptr;
		}


		Number operator[](uint32 index) const {
			if (owner == nullptr) {
				return Number{};
			}
			return owner->buffers[buffer][index];
		}


	private:
		friend FrameCollector;

		FrameCollector *owner;
		uint32 buffer;

		Frame(FrameCollector *ownerIn, uint32 bufferIn) : owner(ownerIn), buffer(bufferIn) {}
	};


private:
	static constexpr uint32 overlap = Size - Hop;

	Number buffers[2][Size] = {};
	// Writer only: the newest published buffer (index plus one, 0 for none), the buffer being filled, the samples in it, and
	// two flags.
	uint32 newest = 0;
	uint32 target = 0;
	uint32 position = 0;
	bool filling = false;
	bool dropping = false;
	bool based = false;
	// Shared: the newest published buffer (written by Push), the buffer the writer is filling or 0 between frames (written by
	// Push), and the buffer the reader holds (written by Latest and by the Frame destructor). All hold index plus one.
	std::atomic<uint32> ready{0};
	std::atomic<uint32> writingTarget{0};
	std::atomic<uint32> leased{0};

	// Reader only: true while a frame from Latest is alive. A second Latest returns an invalid frame until it is released.
	bool held = false;


public:
	// Called from the interrupt, once per sample.
	void Push(Number sample) {
		if (!filling) {
			StartFrame();
		}
		if (!dropping) {
			buffers[target][position] = sample;
		}
		position = position + 1;
		if (position == Size) {
			FinishFrame();
		}
	}


	// The newest complete frame, leased until the returned Frame is destroyed. Call it from one reader only. While a frame is
	// held, a second Latest returns an invalid frame and leaves the lease of the first one alone.
	Frame Latest() {
		if (held) {
			return Frame(nullptr, 0);
		}
		for (uint32 attempt = 0; attempt < 2; attempt++) {
			uint32 reference = ready.load(std::memory_order_acquire);
			if (reference == 0) {
				return Frame(nullptr, 0);
			}
			leased.store(reference, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_seq_cst);
			if (writingTarget.load(std::memory_order_relaxed) != reference) {
				held = true;
				return Frame(this, reference - 1);
			}
			// The writer has just moved to this buffer. Let go and try the newest frame once more.
			leased.store(0, std::memory_order_relaxed);
		}
		return Frame(nullptr, 0);
	}


private:
	void Release() {
		leased.store(0, std::memory_order_release);
		held = false;
	}


	// Chooses the buffer for the next frame, which is the one that is not newest, and checks the reader's lease on it.
	void StartFrame() {
		target = newest == 1 ? 1 : 0;
		writingTarget.store(target + 1, std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_seq_cst);
		dropping = leased.load(std::memory_order_acquire) == target + 1;
		position = 0;
		if (based && !dropping) {
			uint32 source = newest - 1;
			for (uint32 index = 0; index < overlap; index++) {
				buffers[target][index] = buffers[source][Hop + index];
			}
			position = overlap;
		}
		filling = true;
	}


	// Publishes a complete frame, or drops it when the reader holds the buffer. A dropped frame leaves no overlap for the next.
	void FinishFrame() {
		filling = false;
		if (dropping) {
			based = false;
			return;
		}
		newest = target + 1;
		based = true;
		writingTarget.store(0, std::memory_order_relaxed);
		ready.store(newest, std::memory_order_release);
	}
};
