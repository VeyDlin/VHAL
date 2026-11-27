#include <VHAL.h>
#include <VhalCheck.h>
#include <Utilities/Buffer/RingBuffer.h>

#include <cstdint>
#include <deque>


// Host tests of RingBuffer against a model: fill and drain, capacity one, interleaved wraps, the index range near uint16 maximum,
// Clear, Peek, and the marked reads that are committed, rolled back, or refused while pending.


void FillToFullThenEmpty() {
	RingBuffer<uint8, 3> ring;
	VHAL_CHECK_EQ(ring.Push(static_cast<uint8>(1)), ResultStatus::ok);
	VHAL_CHECK_EQ(ring.Push(static_cast<uint8>(2)), ResultStatus::ok);
	VHAL_CHECK_EQ(ring.Push(static_cast<uint8>(3)), ResultStatus::ok);
	VHAL_CHECK(ring.IsFull());
	VHAL_CHECK_EQ(ring.Push(static_cast<uint8>(4)), ResultStatus::filled);
	VHAL_CHECK_EQ(ring.Size(), static_cast<uint16>(3));

	VHAL_CHECK_EQ(ring.Pop().Value(), static_cast<uint8>(1));
	VHAL_CHECK_EQ(ring.Pop().Value(), static_cast<uint8>(2));
	VHAL_CHECK_EQ(ring.Pop().Value(), static_cast<uint8>(3));
	VHAL_CHECK(ring.IsEmpty());

	Result<uint8> empty = ring.Pop();
	VHAL_CHECK(empty.IsErr());
	VHAL_CHECK_EQ(empty.Error(), ResultStatus::empty);
}


void CapacityOne() {
	RingBuffer<uint16, 1> ring;
	for (uint16 i = 0; i < 10; i++) {
		VHAL_CHECK_EQ(ring.Push(i), ResultStatus::ok);
		VHAL_CHECK_EQ(ring.Push(static_cast<uint16>(i + 100)), ResultStatus::filled);
		VHAL_CHECK_EQ(ring.Pop().Value(), i);
		VHAL_CHECK(ring.Pop().IsErr());
	}
}


void InterleavedWrapMatchesModel() {
	// Capacity 5 (not a power of two) with interleaved pushes and pops across many wraps.
	RingBuffer<uint16, 5> ring;
	std::deque<uint16> model;
	uint16 next = 1;
	for (int step = 0; step < 500; step++) {
		if (step % 3 != 2) {
			ResultStatus status = ring.Push(next);
			if (model.size() < 5) {
				VHAL_CHECK_EQ(status, ResultStatus::ok);
				model.push_back(next);
			} else {
				VHAL_CHECK_EQ(status, ResultStatus::filled);
			}
			next++;
		} else {
			Result<uint16> popped = ring.Pop();
			if (model.empty()) {
				VHAL_CHECK(popped.IsErr());
			} else {
				VHAL_CHECK(popped.IsOk());
				VHAL_CHECK_EQ(popped.Value(), model.front());
				model.pop_front();
			}
		}
		VHAL_CHECK_EQ(ring.Size(), static_cast<uint16>(model.size()));
		VHAL_CHECK_EQ(ring.GetFreeSpace(), static_cast<uint16>(5 - model.size()));
	}
}


void IndexTypeNearUint16Max() {
	// Buffer of 65535 elements: indices run up to 65534 and wrap through the uint16 range.
	RingBuffer<uint8, 65535>* ring = new RingBuffer<uint8, 65535>();
	std::deque<uint8> model;
	uint32 next = 0;

	for (uint32 i = 0; i < 65535; i++) {
		VHAL_CHECK_EQ(ring->Push(static_cast<uint8>(next & 0xFF)), ResultStatus::ok);
		model.push_back(static_cast<uint8>(next & 0xFF));
		next++;
	}
	VHAL_CHECK(ring->IsFull());

	for (uint32 round = 0; round < 3; round++) {
		for (uint32 i = 0; i < 40000; i++) {
			VHAL_CHECK_EQ(ring->Pop().Value(), model.front());
			model.pop_front();
		}
		for (uint32 i = 0; i < 40000; i++) {
			VHAL_CHECK_EQ(ring->Push(static_cast<uint8>(next & 0xFF)), ResultStatus::ok);
			model.push_back(static_cast<uint8>(next & 0xFF));
			next++;
		}
		VHAL_CHECK_EQ(ring->Size(), static_cast<uint16>(model.size()));
		VHAL_CHECK_EQ((*ring)[0], model.front());
		VHAL_CHECK_EQ((*ring)[static_cast<uint16>(model.size() - 1)], model.back());
	}
	delete ring;
}


void ClearResetsState() {
	RingBuffer<uint8, 4> ring;
	ring.Push(static_cast<uint8>(9));
	ring.Push(static_cast<uint8>(8));
	VHAL_CHECK_EQ(ring.MarkRead(1), ResultStatus::ok);
	ring.Clear();
	VHAL_CHECK_EQ(ring.Size(), static_cast<uint16>(0));
	VHAL_CHECK_EQ(ring.GetUncommittedCount(), static_cast<uint16>(0));
	VHAL_CHECK(ring.IsEmpty());
	VHAL_CHECK(ring.Pop().IsErr());
	VHAL_CHECK_EQ(ring.Push(static_cast<uint8>(7)), ResultStatus::ok);
	VHAL_CHECK_EQ(ring.Pop().Value(), static_cast<uint8>(7));
}


void PeekAndPeekMultipleWrap() {
	RingBuffer<uint8, 4> ring;
	ring.Push(static_cast<uint8>(1));
	ring.Push(static_cast<uint8>(2));
	ring.Push(static_cast<uint8>(3));
	ring.Pop();
	ring.Pop();
	ring.Push(static_cast<uint8>(4));
	ring.Push(static_cast<uint8>(5));
	ring.Push(static_cast<uint8>(6));
	VHAL_CHECK_EQ(ring.Peek().Value(), static_cast<uint8>(3));

	uint8 out[4] = {};
	uint16 peeked = ring.PeekMultiple(out, 4, 1);
	VHAL_CHECK_EQ(peeked, static_cast<uint16>(3));
	VHAL_CHECK_EQ(out[0], static_cast<uint8>(4));
	VHAL_CHECK_EQ(out[1], static_cast<uint8>(5));
	VHAL_CHECK_EQ(out[2], static_cast<uint8>(6));
}


void MarkCommitAndRollback() {
	RingBuffer<uint8, 4> ring;
	for (uint8 i = 1; i <= 4; i++) {
		ring.Push(i);
	}
	VHAL_CHECK_EQ(ring.MarkRead(2), ResultStatus::ok);
	VHAL_CHECK_EQ(ring.GetUncommittedCount(), static_cast<uint16>(2));
	VHAL_CHECK_EQ(ring.MarkRead(3), ResultStatus::error);
	ring.RollbackReads();
	VHAL_CHECK_EQ(ring.Size(), static_cast<uint16>(4));

	VHAL_CHECK_EQ(ring.MarkRead(2), ResultStatus::ok);
	ring.CommitReads();
	VHAL_CHECK_EQ(ring.Size(), static_cast<uint16>(2));
	VHAL_CHECK_EQ(ring.Pop().Value(), static_cast<uint8>(3));
	VHAL_CHECK_EQ(ring.Pop().Value(), static_cast<uint8>(4));
}


void PopDuringPendingCommitKeepsSizeConsistent() {
	// A Pop while reads are marked removes an element that CommitReads will also
	// try to remove. The size must never underflow past MaxSize().
	RingBuffer<uint8, 4> ring;
	ring.Push(static_cast<uint8>(1));
	ring.Push(static_cast<uint8>(2));
	VHAL_CHECK_EQ(ring.MarkRead(2), ResultStatus::ok);
	ring.Pop();
	ring.CommitReads();
	VHAL_CHECK(ring.Size() <= ring.MaxSize());
	VHAL_CHECK(ring.GetFreeSpace() <= ring.MaxSize());
}


void PopRefusedWhileReadsPendingThenWorksAfterCommit() {
	RingBuffer<uint8, 4> ring;
	ring.Push(static_cast<uint8>(1));
	ring.Push(static_cast<uint8>(2));
	ring.Push(static_cast<uint8>(3));
	VHAL_CHECK_EQ(ring.MarkRead(2), ResultStatus::ok);

	Result<uint8> refused = ring.Pop();
	VHAL_CHECK(refused.IsErr());
	VHAL_CHECK_EQ(refused.Error(), ResultStatus::busy);
	VHAL_CHECK_EQ(ring.Size(), static_cast<uint16>(3));
	VHAL_CHECK_EQ(ring.GetUncommittedCount(), static_cast<uint16>(2));

	ring.CommitReads();
	VHAL_CHECK_EQ(ring.Size(), static_cast<uint16>(1));
	VHAL_CHECK_EQ(ring.Pop().Value(), static_cast<uint8>(3));
	VHAL_CHECK(ring.Pop().IsErr());
}


void PopWorksAfterRollback() {
	RingBuffer<uint8, 4> ring;
	ring.Push(static_cast<uint8>(5));
	ring.Push(static_cast<uint8>(6));
	VHAL_CHECK_EQ(ring.MarkRead(1), ResultStatus::ok);
	VHAL_CHECK_EQ(ring.Pop().Error(), ResultStatus::busy);

	ring.RollbackReads();
	VHAL_CHECK_EQ(ring.Pop().Value(), static_cast<uint8>(5));
	VHAL_CHECK_EQ(ring.Size(), static_cast<uint16>(1));
}


int main() {
	FillToFullThenEmpty();
	CapacityOne();
	InterleavedWrapMatchesModel();
	IndexTypeNearUint16Max();
	ClearResetsState();
	PeekAndPeekMultipleWrap();
	MarkCommitAndRollback();
	PopDuringPendingCommitKeepsSizeConsistent();
	PopRefusedWhileReadsPendingThenWorksAfterCommit();
	PopWorksAfterRollback();

	return VhalCheck::Finish("TestRingBufferModel");
}
