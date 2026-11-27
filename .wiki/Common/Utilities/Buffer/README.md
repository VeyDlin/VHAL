# Buffer

A ring buffer for embedded systems, built on the VHAL framework. Its elements are kept in a storage that the template parameter selects: an array inside the object, a pinned block of a heap, or memory the caller gives.

---

## Table of Contents

- [Quick Start](#quick-start)
- [RingBuffer](#ringbuffer)
  - [Basic Push / Pop](#basic-push--pop)
  - [Peek and Indexed Access](#peek-and-indexed-access)
  - [Transactional Reads](#transactional-reads)
  - [Storage](#storage)
  - [API Reference](#ringbuffer-api-reference)
- [Thread Safety](#thread-safety)

---

## Quick Start

```cpp
#include <Utilities/Buffer/RingBuffer.h>

// A ring buffer holding up to 64 uint8 elements
RingBuffer<uint8, 64> rx;

rx.Push(0xAA);
rx.Push(0xBB);

auto result = rx.Pop();
if (result.IsOk()) {
    uint8 byte = result.Value(); // 0xAA
}
```

`Push` and `Clear` return a `ResultStatus`. `Pop` and `Peek` return a `Result<T>`: check `IsOk()`, read the element with `Value()`, and read the failure reason with `Error()` when `IsErr()` is true.

---

## RingBuffer

```cpp
template<typename ElementType, uint16 BufferSize, typename Policy = Memory::Inside>
class RingBuffer;
```

A fixed-capacity circular (ring) buffer. When full, `Push` returns `ResultStatus::filled` rather than overwriting old data. Supports transactional reads via `MarkRead` / `CommitReads` / `RollbackReads`, which let you peek at data and only remove it once processing succeeds.

`Policy` is the storage policy of the ring (see [Storage](#storage)). It defaults to `Memory::Inside`, so `RingBuffer<T, N>` keeps the elements in the object.

### Basic Push / Pop

```cpp
RingBuffer<uint16, 128> buf;

buf.Push(1000);
buf.Push(2000);
buf.Push(3000);

buf.Size();      // 3
buf.MaxSize();   // 128
buf.GetFreeSpace(); // 125

auto val = buf.Pop();  // val.Value() == 1000, val.IsOk() == true
auto val2 = buf.Pop(); // val2.Value() == 2000

buf.Clear(); // empties the buffer
```

Push also accepts a pointer:

```cpp
uint16 value = 42;
buf.Push(&value);
```

Bulk calls copy up to the free space (`Push(std::span<const ElementType>)`) or up to the size of the output span (`Pop(std::span<ElementType>)`), in one critical section, and return the count moved.

### Peek and Indexed Access

```cpp
RingBuffer<char, 32> buf;
buf.Push('A');
buf.Push('B');
buf.Push('C');

// Peek at the front element without removing it
auto front = buf.Peek(); // front.Value() == 'A'; front.IsOk() is false when the buffer is empty

// Random access by index (0 = oldest element)
char second = buf[1]; // 'B'

// Peek multiple elements into a local array
char out[2];
uint16 count = buf.PeekMultiple(out, 2);       // out = {'A','B'}, count = 2
uint16 count2 = buf.PeekMultiple(out, 2, 1);   // out = {'B','C'}, offset = 1
```

### Transactional Reads

Transactional reads let you inspect data, then either commit (remove) or rollback (keep) it. This is useful when you need to parse a message from the buffer but want to keep the data if parsing fails.

```cpp
RingBuffer<uint8, 256> buf;
// ... assume buf has data ...

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
```

### Storage

The third template argument selects where the elements are kept. Each policy is in `Utilities/Memory/Storage.h`.

| Policy | Elements are | Object holds | Use |
|---|---|---|---|
| `Memory::Inside` (default) | an array of the object | the elements and the indexes | a fixed ring in a static or stack object |
| `Memory::FromHeap<heap>` | a pinned block of `heap` | a pointer and the indexes | a ring whose size is chosen from a heap; the block is freed with the ring |
| `Memory::External` | memory the caller passes | a pointer, a length and the indexes | a ring over a buffer that a DMA or another module owns |

```cpp
RingBuffer<uint8, 256> fixed;                                            // Memory::Inside

Memory::Heap<> heap(arena, sizeof(arena));                                // a static object
RingBuffer<uint8, 256, Memory::FromHeap<heap>> pooled;                    // a pinned block of heap

uint8 dmaBuffer[256];
RingBuffer<uint8, 256, Memory::External> given{std::span<uint8>(dmaBuffer)};
```

- `FromHeap` takes its block when the ring is constructed and releases it when the ring is destroyed. The heap must be a static object, and it must be constructed before the ring. A debug build asserts this. A ring that is a static object in another translation unit is built on first use (a function-local static), so the heap exists by then.
- `External` needs a span of at least `BufferSize` elements. A shorter span leaves the ring not ready.
- A ring that is not ready refuses its writes: `Push` returns `ResultStatus::notReady` and the bulk `Push` returns 0. `Size()` stays 0, and `Pop` and `Peek` return empty.
- The policy changes only where the elements are. The operations, the critical sections and the transactional reads are the same for every policy.

Memory costs, measured on Cortex-M0+ and M4 with a ring of 16 `uint8` elements:

- `Inside` takes 22 bytes in the object (16 elements and 6 bytes of indexes).
- `FromHeap` takes 12 bytes in the object (a pointer and the indexes). Its block takes the heap's block header (12 bytes on Cortex-M0+) and the elements, with the block size rounded up to 8 bytes. A ring of 16 bytes therefore costs 40 bytes in all, 18 bytes more than `Inside`.
- The flash cost of `FromHeap` is the heap's allocator, once per program (about 1 KB of text on Cortex-M0+), and about 96 bytes of text for each further ring that uses it (16 bytes for an `Inside` ring of the same type).

### RingBuffer API Reference

| Method | Return | Description |
|---|---|---|
| `Push(const ElementType)` | `ResultStatus` | Push element by value. Returns `ResultStatus::filled` if full, `ResultStatus::notReady` if the storage is not ready. |
| `Push(const ElementType*)` | `ResultStatus` | Push element by pointer. Returns `ResultStatus::error` if null, `ResultStatus::notReady` if the storage is not ready. |
| `Push(std::span<const ElementType>)` | `uint16` | Push as many elements as fit. Returns the count pushed (0 if the storage is not ready). |
| `Pop()` | `Result<ElementType>` | Remove and return the oldest element. Returns `ResultStatus::empty` if empty, and `ResultStatus::busy` (nothing removed) while elements are marked by `MarkRead`; call `CommitReads` or `RollbackReads` first. |
| `Pop(std::span<ElementType>)` | `uint16` | Remove up to `out.size()` oldest elements into `out`. Returns the count, 0 while elements are marked. |
| `Peek()` | `Result<ElementType>` | Return the oldest element without removing it. Returns `ResultStatus::empty` if empty. |
| `PeekMultiple(out, count, offset)` | `uint16` | Copy up to `count` elements starting at `offset` into `out`. Returns actual count. |
| `operator[](uint16)` | `ElementType` | Access element by index (0 = oldest). Asserts if out of range. |
| `IsFull()` | `bool` | True if buffer is at capacity. |
| `IsEmpty()` | `bool` | True if buffer has no elements. |
| `Size()` | `uint16` | Current number of elements. |
| `MaxSize()` | `uint16` | Buffer capacity (template parameter). |
| `GetFreeSpace()` | `uint16` | Number of elements that can still be pushed. |
| `Clear()` | `void` | Remove all elements. |
| `MarkRead(count)` | `ResultStatus` | Mark `count` elements as tentatively read. Returns `ResultStatus::error` (nothing marked) if `count` exceeds the unmarked elements. |
| `CommitReads()` | `void` | Remove all elements marked by `MarkRead`. |
| `RollbackReads()` | `void` | Cancel all uncommitted marks. |
| `GetUncommittedCount()` | `uint16` | Number of elements currently marked but not committed. |

---

## Thread Safety

Every operation that changes the ring uses `System::CriticalSection(true/false)` to guard its state, so the ring is safe to use from an interrupt or from a concurrent context in a bare-metal or RTOS environment. A bulk call holds one critical section for the whole copy.
