#include <VHAL.h>
#include <VhalCheck.h>
#include <Utilities/Buffer/RingBuffer.h>


// The RingBuffer examples of the Buffer wiki page, run as the page writes them. The values the examples state in their comments
// are checked. TransactionalReads pushes four elements before its reads, marked in that function, so that the example has data to
// read. The examples of the fixed-size and dynamic FIFOs are not here: those classes are not part of VHAL.


void QuickStart() {
	// A ring buffer holding up to 64 uint8 elements
	RingBuffer<uint8, 64> rx;

	rx.Push(0xAA);
	rx.Push(0xBB);

	Result<uint8> result = rx.Pop();
	if (result.IsOk()) {
		uint8 byte = result.Value(); // 0xAA
		VHAL_CHECK_EQ(byte, static_cast<uint8>(0xAA));
	}
}


void BasicPushPop() {
	RingBuffer<uint16, 128> buf;

	buf.Push(1000);
	buf.Push(2000);
	buf.Push(3000);

	VHAL_CHECK_EQ(buf.Size(), static_cast<uint16>(3));     // 3
	VHAL_CHECK_EQ(buf.MaxSize(), static_cast<uint16>(128)); // 128
	VHAL_CHECK_EQ(buf.GetFreeSpace(), static_cast<uint16>(125)); // 125

	Result<uint16> val = buf.Pop();  // val.Value() == 1000, val.IsOk() == true
	Result<uint16> val2 = buf.Pop(); // val2.Value() == 2000
	VHAL_CHECK(val.IsOk());
	VHAL_CHECK_EQ(val.Value(), static_cast<uint16>(1000));
	VHAL_CHECK_EQ(val2.Value(), static_cast<uint16>(2000));

	buf.Clear(); // empties the buffer
	VHAL_CHECK(buf.IsEmpty());
}


void PushPointer() {
	RingBuffer<uint16, 128> buf;
	uint16 value = 42;
	buf.Push(&value);
}


void PeekAndIndex() {
	RingBuffer<char, 32> buf;
	buf.Push('A');
	buf.Push('B');
	buf.Push('C');

	// Peek at the front element without removing it
	Result<char> front = buf.Peek(); // front.Value() == 'A'; front.IsOk() is false when the buffer is empty
	VHAL_CHECK_EQ(front.Value(), 'A');

	// Random access by index (0 = oldest element)
	char second = buf[1]; // 'B'
	VHAL_CHECK_EQ(second, 'B');

	// Peek multiple elements into a local array
	char out[2];
	uint16 count = buf.PeekMultiple(out, 2);       // out = {'A','B'}, count = 2
	VHAL_CHECK_EQ(count, static_cast<uint16>(2));
	VHAL_CHECK_EQ(out[0], 'A');
	VHAL_CHECK_EQ(out[1], 'B');

	uint16 count2 = buf.PeekMultiple(out, 2, 1);   // out = {'B','C'}, offset = 1
	VHAL_CHECK_EQ(count2, static_cast<uint16>(2));
	VHAL_CHECK_EQ(out[0], 'B');
	VHAL_CHECK_EQ(out[1], 'C');
}


void TransactionalReads() {
	RingBuffer<uint8, 256> buf;
	// The buffer holds data: four elements are pushed for the example to read.
	buf.Push(static_cast<uint8>(1));
	buf.Push(static_cast<uint8>(2));
	buf.Push(static_cast<uint8>(3));
	buf.Push(static_cast<uint8>(4));

	// Peek at data without removing
	uint8 header[4];
	buf.PeekMultiple(header, 4);

	// Mark elements as "tentatively read"
	buf.MarkRead(4);

	// If processing succeeds, commit
	buf.CommitReads();

	// If processing fails, rollback -- data stays in buffer
	buf.RollbackReads();

	// Check how many reads are pending
	uint16 pending = buf.GetUncommittedCount();
	VHAL_CHECK_EQ(pending, static_cast<uint16>(0));
	VHAL_CHECK_EQ(buf.Size(), static_cast<uint16>(0));
}


int main() {
	QuickStart();
	BasicPushPop();
	PushPointer();
	PeekAndIndex();
	TransactionalReads();

	return VhalCheck::Finish("TestRingBufferReadme");
}
