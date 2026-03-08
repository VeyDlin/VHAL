# Buffer

A collection of thread-safe buffer and FIFO data structures for embedded systems, built on the VHAL framework.

---

## Table of Contents

- [Quick Start](#quick-start)
- [RingBuffer](#ringbuffer)
  - [Basic Push / Pop](#basic-push--pop)
  - [Peek and Indexed Access](#peek-and-indexed-access)
  - [Transactional Reads](#transactional-reads)
  - [API Reference](#ringbuffer-api-reference)
- [StaticFifo](#staticfifo)
  - [Usage](#staticfifo-usage)
  - [API Reference](#staticfifo-api-reference)
- [DynamicFifo](#dynamicfifo)
  - [Usage](#dynamicfifo-usage)
  - [API Reference](#dynamicfifo-api-reference)
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
if (result.type == Status::ok) {
    uint8 byte = result.data; // 0xAA
}
```

---

## RingBuffer

```cpp
template<typename ElementType, uint16 BufferSize>
class RingBuffer;
```

A fixed-capacity circular (ring) buffer. When full, `Push` returns `Status::filled` rather than overwriting old data. Supports transactional reads via `MarkRead` / `CommitReads` / `RollbackReads`, which let you peek at data and only remove it once processing succeeds.

### Basic Push / Pop

```cpp
RingBuffer<uint16, 128> buf;

buf.Push(1000);
buf.Push(2000);
buf.Push(3000);

buf.Size();      // 3
buf.MaxSize();   // 128
buf.GetFreeSpace(); // 125

auto val = buf.Pop();  // val.data == 1000, val.type == Status::ok
auto val2 = buf.Pop(); // val2.data == 2000

buf.Clear(); // empties the buffer
```

Push also accepts a pointer:

```cpp
uint16 value = 42;
buf.Push(&value);
```

### Peek and Indexed Access

```cpp
RingBuffer<char, 32> buf;
buf.Push('A');
buf.Push('B');
buf.Push('C');

// Peek at the front element without removing it
auto front = buf.Peek(); // front.data == 'A'

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

// Step 1: Peek at data without removing
uint8 header[4];
buf.PeekMultiple(header, 4);

// Step 2: Mark elements as "tentatively read"
buf.MarkRead(4);

// Step 3a: If processing succeeds, commit
buf.CommitReads();

// Step 3b: If processing fails, rollback -- data stays in buffer
buf.RollbackReads();

// Check how many reads are pending
uint16 pending = buf.GetUncommittedCount();
```

### RingBuffer API Reference

| Method | Return | Description |
|---|---|---|
| `Push(const ElementType)` | `Status::statusType` | Push element by value. Returns `Status::filled` if full. |
| `Push(const ElementType*)` | `Status::statusType` | Push element by pointer. Returns `Status::error` if null. |
| `Pop()` | `Status::info<ElementType>` | Remove and return the oldest element. Returns `Status::empty` if empty. |
| `Peek()` | `Status::info<ElementType>` | Return the oldest element without removing it. |
| `PeekMultiple(out, count, offset)` | `uint16` | Copy up to `count` elements starting at `offset` into `out`. Returns actual count. |
| `operator[](uint16)` | `ElementType` | Access element by index (0 = oldest). Asserts if out of range. |
| `IsFull()` | `bool` | True if buffer is at capacity. |
| `IsEmpty()` | `bool` | True if buffer has no elements. |
| `Size()` | `uint16` | Current number of elements. |
| `MaxSize()` | `uint16` | Buffer capacity (template parameter). |
| `GetFreeSpace()` | `uint16` | Number of elements that can still be pushed. |
| `Clear()` | `void` | Remove all elements. |
| `MarkRead(count)` | `Status::statusType` | Mark `count` elements as tentatively read. |
| `CommitReads()` | `void` | Remove all elements marked by `MarkRead`. |
| `RollbackReads()` | `void` | Cancel all uncommitted marks. |
| `GetUncommittedCount()` | `uint16` | Number of elements currently marked but not committed. |

**Callback:**

| Field | Type | Description |
|---|---|---|
| `onLook` | `std::function<void(bool)>` | Optional callback, assignable by user. Not called internally. |

---

## StaticFifo

```cpp
template <size_t ElementsCount, typename Type = uint8>
class StaticFifo;
```

A fixed-capacity FIFO queue for elements of a single type. Elements are stored in a contiguous byte array and shifted on pop (not a ring buffer). Best suited for small queues where simplicity matters more than pop performance.

### StaticFifo Usage

```cpp
StaticFifo<8, uint32> fifo; // up to 8 uint32 elements

fifo.Push(100);   // Status::ok
fifo.Push(200);   // Status::ok
fifo.GetCount();  // 2

auto result = fifo.Pop();
if (result.type == Status::ok) {
    uint32 val = result.data; // 100
}

fifo.Clear();
fifo.IsReady();   // true unless another operation is in progress
```

### StaticFifo API Reference

| Method | Return | Description |
|---|---|---|
| `Push(const Type&)` | `Status::statusType` | Add an element. Returns `Status::filled` if full, `Status::busy` if not ready. |
| `Pop()` | `Status::info<Type>` | Remove and return the oldest element. Returns `Status::empty` / `Status::busy`. |
| `Clear()` | `Status::statusType` | Empty the queue. Returns `Status::busy` if not ready. |
| `GetCount()` | `size_t` | Number of elements currently stored. |
| `IsReady()` | `bool` | False if another operation is in progress. |

---

## DynamicFifo

```cpp
template <size_t heapSize, typename dataSizeType = uint8>
class DynamicFifo;
```

A FIFO queue for variable-length data packets. Each entry is stored as a size prefix (`dataSizeType`) followed by the raw bytes. This is useful for queuing messages or packets of different sizes in a single buffer.

The `dataSizeType` template parameter controls the maximum size of a single entry (e.g., `uint8` allows entries up to 255 bytes, `uint16` up to 65535).

### DynamicFifo Usage

```cpp
DynamicFifo<512, uint16> fifo;

// Push a struct
struct Packet { uint8 id; uint16 value; };
Packet pkt = { 1, 0xBEEF };
fifo.Push(pkt, sizeof(Packet)); // returns true on success

// Pop into a buffer
Packet out;
size_t readSize = fifo.Pop(out); // readSize == sizeof(Packet)

// Push raw byte arrays
uint8 data[10] = { /* ... */ };
fifo.Push(data, 10);

uint8 outData[10];
fifo.Pop(outData); // returns 10

fifo.GetCount(); // bytes currently used in the heap
fifo.Clear();
```

### DynamicFifo API Reference

| Method | Return | Description |
|---|---|---|
| `Push(const dataType&, dataSizeType)` | `bool` | Push `dataSize` bytes from `data`. Returns false if not enough space or busy. |
| `Pop(dataType&)` | `size_t` | Pop the oldest entry into `data`. Returns the byte count, or 0 if empty/busy. |
| `Clear()` | `bool` | Empty the buffer. Returns false if busy. |
| `GetCount()` | `size_t` | Total bytes currently occupied in the heap (including size prefixes). |
| `IsReady()` | `bool` | False if another operation is in progress. |

---

## Thread Safety

All three classes use `System::CriticalSection(true/false)` to guard their internal state, making them safe to use from interrupts or concurrent contexts in a bare-metal or RTOS environment. The `StaticFifo` and `DynamicFifo` classes additionally use an `isReady` flag to detect reentrant access and return a busy/failure status rather than corrupting data.
