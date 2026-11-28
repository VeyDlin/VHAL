# Buffer

Ring buffers and packet queues for embedded systems, built on the VHAL framework. Their elements are kept in a storage that the template parameter selects: an array inside the object, a pinned block of a heap, or memory the caller gives. Their access is chosen by a template parameter too: a critical section for each call, one producer and one consumer without a section, or a caller that serializes the calls itself.

---

## Table of Contents

- [Quick Start](#quick-start)
- [RingBuffer](#ringbuffer)
  - [Basic Push / Pop](#basic-push--pop)
  - [Peek and Indexed Access](#peek-and-indexed-access)
  - [Transactional Reads](#transactional-reads)
  - [Storage](#storage)
  - [Access Modes](#access-modes)
  - [Members by Mode](#members-by-mode)
  - [Pointer Lifetime](#pointer-lifetime)
  - [Cost of the Modes](#cost-of-the-modes)
  - [RingBuffer API Reference](#ringbuffer-api-reference)
- [PacketQueue](#packetqueue)
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

`Push` returns a `ResultStatus`, and `Clear` returns nothing. `Pop` and `Peek` return a `Result<T>`: check `IsOk()`, read the element with `Value()`, and read the failure reason with `Error()` when `IsErr()` is true. `Front` and `Peek(index)` return a pointer, which is `nullptr` when no element is held.

---

## RingBuffer

```cpp
template<typename ElementType, uint16 BufferSize, typename Policy = Memory::Inside, RingAccess Access = RingAccess::locked>
class RingBuffer;
```

A fixed-capacity circular (ring) buffer of `BufferSize` elements. When full, `Push` returns `ResultStatus::filled` rather than overwriting old data. Supports transactional reads via `MarkRead` / `CommitReads` / `RollbackReads`, which let you peek at data and only remove it once processing succeeds.

`Policy` is the storage policy of the ring (see [Storage](#storage)). It defaults to `Memory::Inside`, so `RingBuffer<T, N>` keeps the elements in the object. `Access` is the access mode (see [Access Modes](#access-modes)). It defaults to `RingAccess::locked`.

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

Bulk calls copy up to the free space (`Push(std::span<const ElementType>)`) or up to the size of the output span (`Pop(std::span<ElementType>)`) and return the count copied. In locked mode the whole copy runs in one critical section.

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

// Pointers to the elements, in place: nullptr when the index is not held
const char* oldest = buf.Front();  // *oldest == 'A'
const char* last = buf.Peek(2);    // *last == 'C'
const char* none = buf.Peek(3);    // nullptr
buf.Drop(2);                       // removes 'A' and 'B'; the front is 'C'
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

`Pop` and `Drop` remove nothing while marks are pending, as they refuse to take elements that a transaction has not decided on.

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
- `External` needs a span of at least as many elements as the ring's storage: `BufferSize` in locked and callerLocked mode, and `BufferSize + 1` in singleWriter mode, which keeps one slot empty for its indexes. A shorter span leaves the ring not ready.
- A ring that is not ready refuses its writes: `Push` returns `ResultStatus::notReady` and the bulk `Push` returns 0. `Size()` stays 0, and `Pop` and `Peek` return empty.
- The policy changes only where the elements are. The operations and the transactional reads are the same for every policy.

Memory costs of the locked mode, measured on Cortex-M0+ and M4 with a ring of 16 `uint8` elements:

- `Inside` takes 22 bytes in the object (16 elements and 6 bytes of indexes).
- `FromHeap` takes 12 bytes in the object (a pointer and the indexes). Its block takes the heap's block header (12 bytes on Cortex-M0+) and the elements, with the block size rounded up to 8 bytes. A ring of 16 bytes therefore costs 40 bytes in all, 18 bytes more than `Inside`.
- The flash cost of `FromHeap` is the heap's allocator, once per program (about 1 KB of text on Cortex-M0+), and about 96 bytes of text for each further ring that uses it (16 bytes for an `Inside` ring of the same type).

### Access Modes

`RingAccess` selects how the ring is shared between contexts:

| Mode | Who calls | Critical section | Barrier | Storage | Fits |
|---|---|---|---|---|---|
| `RingAccess::locked` (default) | any context, any member | one per call, and one for a whole bulk copy | none needed | `BufferSize` elements | several writers, such as a task and an interrupt that both push |
| `RingAccess::singleWriter` | one producer calls `Push`; one consumer calls every other member | none | release store after the data, acquire load of the other index | `BufferSize + 1` elements | an interrupt that produces and a main loop that consumes |
| `RingAccess::callerLocked` | the caller makes sure one call runs at a time | none | none | `BufferSize` elements | one thread, or a caller that already holds a section |

A singleWriter ring takes no critical section, so the producer may be an interrupt that runs while the consumer is in the middle of a call. Each index is written by one context only, and the release and acquire orders make the data of an element visible before the index that publishes it. A callerLocked ring keeps the read index and the count that a locked ring keeps, with no critical section and no barrier.

```cpp
// The receive interrupt pushes the byte it received.
RingBuffer<uint8, 256, Memory::Inside, RingAccess::singleWriter> rxBytes;

// Interrupt context
rxBytes.Push(static_cast<uint8>(0x5A));

// Main loop
Result<uint8> byte = rxBytes.Pop();
```

```cpp
// The consumer empties the ring with Drop, since Clear is not offered in singleWriter mode.
RingBuffer<uint8, 8, Memory::Inside, RingAccess::singleWriter> ring;
ring.Push(static_cast<uint8>(1));
ring.Push(static_cast<uint8>(2));
ring.Drop(ring.Size()); // returns 2
```

The mode that fits a case:

- An interrupt produces and the main loop consumes: `singleWriter`.
- One thread uses the ring, or a layer that already holds a critical section calls it: `callerLocked`.
- Several contexts push to the same ring: `locked`.

### Members by Mode

| Member | `locked` | `singleWriter` | `callerLocked` |
|---|---|---|---|
| `Push` (all three forms) | any context | producer only | caller serializes |
| `Pop()`, `Pop(span)`, `Peek()`, `operator[]`, `PeekMultiple`, `Front()`, `Peek(index)`, `Drop`, `MarkRead`, `CommitReads`, `RollbackReads`, `GetUncommittedCount` | any context | consumer only | caller serializes |
| `Size`, `IsFull`, `IsEmpty`, `GetFreeSpace`, `MaxSize` | any context | a snapshot of both indexes | caller serializes |
| `Clear` | any context | not offered: a compile error that names `Drop` | caller serializes |

`Front()` and `Peek(index)` are offered in every mode. `Drop(count)` removes up to `count` of the oldest elements and returns how many, in every mode.

A call of a member that a mode does not offer is a compile error. The error names the reason, such as `RingBuffer::Clear is not offered in singleWriter mode`.

### Pointer Lifetime

`Front()` and `Peek(index)` return a pointer to the element in the storage. The pointer stays valid until that element is popped or dropped. A consumer reads the element through the pointer and then calls `Pop` or `Drop` to release it.

In locked mode the caller makes sure that no other context pops or drops the element while the pointer is in use: either one consumer context runs, or the caller holds a critical section. A producer never moves an element, so a push does not invalidate the pointer.

### Cost of the Modes

Measured with the size table of the module (`-Os`), with the ring's critical section stubbed, so the locked row excludes the section of a port. The text is the cost over a program that makes the same call with no ring, and RAM is the data and bss of the image over that program. Each program pushes 32 bytes and reads them back: a ring pushes each byte and reads it in place with `Front()` and `Drop(1)`, and a packet queue pushes 4-byte packets and reads them the same way. The rows of a program make the same calls. The VUB row is the single-core ring of VUB, which has its own operations (a push and a pop of a byte), so it is not a row of the same calls.

| Program | Cortex-M0+ text | Cortex-M4 text | RAM |
|---|---:|---:|---:|
| `RingBuffer<uint8, 64>`, `locked` | 136 | 108 | 72 |
| `RingBuffer<uint8, 64>`, `singleWriter` | 212 | 204 | 72 |
| `RingBuffer<uint8, 64>`, `callerLocked` | 136 | 108 | 72 |
| `PacketQueue<64, 8>`, `locked` | 220 | 156 | 540 |
| `PacketQueue<64, 8>`, `singleWriter` | 288 | 232 | 604 |
| `PacketQueue<64, 8>`, `callerLocked` | 220 | 156 | 540 |
| VUB single-core ring form, 64 slots (VUB's own operations) | 92 | 88 | 68 |

The singleWriter mode costs the most text of the three modes: its operations load the other context's index with an acquire load and publish with a release store. A packet queue in singleWriter mode holds one more slot, so its RAM is larger.

### RingBuffer API Reference

| Method | Return | Description |
|---|---|---|
| `Push(const ElementType)` | `ResultStatus` | Push element by value. Returns `ResultStatus::filled` if full, `ResultStatus::notReady` if the storage is not ready. |
| `Push(const ElementType*)` | `ResultStatus` | Push element by pointer. Returns `ResultStatus::error` if null, `ResultStatus::notReady` if the storage is not ready. |
| `Push(std::span<const ElementType>)` | `uint16` | Push as many elements as fit. Returns the count pushed (0 if the storage is not ready). |
| `Pop()` | `Result<ElementType>` | Remove and return the oldest element. Returns `ResultStatus::empty` if empty, and `ResultStatus::busy` (nothing removed) while elements are marked by `MarkRead`; call `CommitReads` or `RollbackReads` first. |
| `Pop(std::span<ElementType>)` | `uint16` | Remove up to `out.size()` oldest elements into `out`. Returns the count, 0 while elements are marked. |
| `Front()` | `const ElementType*` | The oldest element in place, or `nullptr` when empty. See [Pointer Lifetime](#pointer-lifetime). |
| `Peek()` | `Result<ElementType>` | Return the oldest element without removing it. Returns `ResultStatus::empty` if empty. |
| `Peek(index)` | `const ElementType*` | The element at `index` (0 = oldest) in place, or `nullptr` when `index` is not held. |
| `Drop(count)` | `uint16` | Remove up to `count` oldest elements and return how many. Returns 0 while elements are marked. |
| `PeekMultiple(out, count, offset)` | `uint16` | Copy up to `count` elements starting at `offset` into `out`. Returns actual count. |
| `operator[](uint16)` | `ElementType` | Access element by index (0 = oldest). Asserts if out of range. |
| `IsFull()` | `bool` | True if buffer is at capacity. |
| `IsEmpty()` | `bool` | True if buffer has no elements. |
| `Size()` | `uint16` | Current number of elements. |
| `MaxSize()` | `uint16` | Buffer capacity (template parameter). |
| `GetFreeSpace()` | `uint16` | Number of elements that can still be pushed. |
| `Clear()` | `void` | Remove all elements and marks. Not offered in singleWriter mode. |
| `MarkRead(count)` | `ResultStatus` | Mark `count` elements as tentatively read. Returns `ResultStatus::error` (nothing marked) if `count` exceeds the unmarked elements. |
| `CommitReads()` | `void` | Remove all elements marked by `MarkRead`. |
| `RollbackReads()` | `void` | Cancel all uncommitted marks. |
| `GetUncommittedCount()` | `uint16` | Number of elements currently marked but not committed. |

---

## PacketQueue

```cpp
template<std::size_t SlotBytes, uint16 Slots, typename Policy = Memory::Inside, RingAccess Access = RingAccess::locked>
class PacketQueue;
```

A queue of `Slots` packets. Each packet is at most `SlotBytes` bytes long, and each slot keeps the length of its packet and one flags byte. The length is a `uint8` when `SlotBytes` is 255 or less, and a `uint16` otherwise. The queue takes the same access modes and storage policies as a ring.

```cpp
PacketQueue<64, 8> packets; // up to 8 packets of 64 bytes

const uint8 command[] = {0x01, 0x02, 0x03};
packets.Push(std::span<const uint8>(command), 0x00); // the flags byte of the packet

std::span<const uint8> packet;
uint8 flags = 0;
if (packets.Front(packet, flags) == ResultStatus::ok) {
    // packet.size() == 3, packet[2] == 0x03
}

packets.Pop();
```

| Method | Return | Description |
|---|---|---|
| `Push(std::span<const uint8>, uint8 flags)` | `ResultStatus` | Put a packet at the back. `messageTooLong` when it is longer than a slot, `busy` when the queue is full. |
| `Front(packet, flags)` | `ResultStatus` | The oldest packet as a span, in place, and its flags. `empty` when there is no packet. |
| `Peek(index, packet, flags)` | `ResultStatus` | The packet at `index` (0 = oldest) as a span, in place. `outOfRange` when `index` is not held. |
| `Pop()` | `ResultStatus` | Remove the oldest packet. `empty` when there is none. |
| `Drop(count)` | `uint16` | Remove up to `count` oldest packets and return how many. |
| `DropFlagged(flag)` | `uint16` | Remove every packet whose flags have the bit `flag`, keep the others in their order, and return how many were removed. Not offered in singleWriter mode. |
| `Count()`, `Full()`, `Empty()` | `uint16`, `bool`, `bool` | The number of packets held, and whether there are none or the maximum. |
| `Clear()` | `void` | Remove all packets. Not offered in singleWriter mode. |

`DropFlagged` rewrites the packets and the count of the queue, which the producer must not change during the call. It is offered in locked mode, where the call holds a critical section for its whole run, and in callerLocked mode, where the caller keeps any push out during the call. In singleWriter mode the producer owns the head, so the consumer drops packets with `Drop`.

```cpp
PacketQueue<64, 8, Memory::Inside, RingAccess::locked> queue;

const uint8 urgent[] = {0xEE};
const uint8 normal[] = {0x11};
queue.Push(std::span<const uint8>(urgent), 0x01);
queue.Push(std::span<const uint8>(normal), 0x00);

uint16 dropped = queue.DropFlagged(0x01); // the urgent packet is dropped, the normal one stays
```

A packet queue with `Memory::External` takes three memory blocks: the bytes of `Slots` slots, and the lengths and the flags of `Slots` entries each. In singleWriter mode the blocks hold `Slots + 1` slots and entries, because that mode keeps one slot empty for its indexes.

```cpp
// Eight slots of 64 bytes are held, so the memory needs eight slots.
uint8 bytes[8 * 64];
uint8 lengths[8];
uint8 flags[8];

PacketQueue<64, 8, Memory::External, RingAccess::callerLocked> queue{
    std::span<uint8>(bytes),
    std::span<uint8>(lengths),
    std::span<uint8>(flags)};
```

---

## Thread Safety

A `locked` ring or queue takes `System::CriticalSection(true/false)` around each call, so it is safe to use from an interrupt or from a concurrent context in a bare-metal or RTOS environment. A bulk call holds one critical section for the whole copy.

A `singleWriter` ring or queue is safe for one producer context and one consumer context, with no critical section. The producer is the only context that changes the head, and the consumer is the only context that changes the tail. The mode is not safe for more than one producer or more than one consumer.

A `callerLocked` ring or queue takes no critical section. The caller is responsible for serializing every call, for example by holding its own section or by running in one thread.
