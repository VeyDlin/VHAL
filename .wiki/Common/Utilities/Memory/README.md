# Memory

Heap, handles and the containers that keep their data in a heap block. Namespace `Memory`, folder `Common/Utilities/Memory/`.

| Header | Contents |
|---|---|
| `Memory/Heap.h` | `Heap`, the defragmentation, lock and out-of-memory policies, pinned blocks |
| `Memory/Handle.h` | `Handle` and the entry table slot `Entry` |
| `Memory/IAllocator.h` | `IAllocator` and `AllocatorOf` (the virtual view of a heap) |
| `Memory/Vector.h`, `String.h`, `PoolAllocator.h`, `SharedPtr.h` | containers and a pool, each with an explicit allocator |

## Heap

```cpp
#include <Utilities/Memory/Heap.h>

uint8_t buffer[4096];
Memory::Heap<> heap(buffer, sizeof(buffer));           // external buffer
Memory::Heap<> owned(4096);                            // internal buffer, from operator new[]

Memory::Handle block = heap.Allocate(64);
void* data = block.Data();                             // valid until the next call that can move the block
heap.Deallocate(block);

Memory::AllocatorOf<Memory::Heap<>> adapter(heap);     // the same heap as an IAllocator, for containers
```

The template takes three policies. Each one has a default, so `Memory::Heap<>` is the plain heap.

| Parameter | Default | Other choices |
|---|---|---|
| Defragmentation | `NoDefrag` (no block moves) | `IncrementalDefrag<N>` (N steps per `Defragment()`), `BatchDefrag<N>` (the same), `ThresholdDefrag<Percent>` (one step when fragmentation reaches Percent) |
| Lock | `NoLock` (no lock code) | any type with `static void Lock()` and `static void Unlock()` |
| Out of memory | `NoOom` (nothing happens) | `OomHandler<&Function>`: calls `Function()` when a request is refused |

```cpp
void OnFull() {
}

Memory::Heap<Memory::IncrementalDefrag<4>, Memory::NoLock, Memory::OomHandler<&OnFull>> heap(buffer, sizeof(buffer));
```

Queries: `TotalBytes()` (usable capacity), `UsedBytes()` (sizes of the used blocks), `FreeBytes()` (the unallocated tail, less the entry table, plus the free blocks), `FragmentationPercent()` (free bytes between used blocks, as a share of the used region), `IsConstructed()` (debug builds only check it; always true with `NDEBUG`).

Heap objects are not copyable. A heap does not derive from `IAllocator`, so a program that names the heap type pays for no virtual call. Code that takes any heap at run time takes an `IAllocator&`, and a heap is passed through an adapter. The adapter must not outlive the heap.

## Handle

A `Handle` is an index into the entry table with a generation. `Data()` returns the current address, `Size()` the size of the request, and `IsValid()` is false for an empty handle, a freed block, and a copy whose slot was reused. Only a `Handle` stays valid across `Defragment()`. Copies of a handle are valid until the block is freed.

A handle of another heap, or of a pool, is ignored by `Deallocate` and `IsPinned`. `Reallocate` copies its data into a new block of this heap and leaves the old block to its owner.

## Blocks

Each block has a 12-byte header on a 32-bit target: the entry, the size, the free and pinned flags and the offset from the header to the data. The entry of a block holds the same offset, so `Deallocate` and `Reallocate` find the header from the handle without a walk over the blocks. The data of a movable block is 8-byte aligned. When a block moves, its data keeps its alignment because the offset is set again for the new position.

Only the header lookup avoids the walk. Freeing a block walks the blocks from the start to find the block before it, because a block has no link to its predecessor. So `Deallocate` costs one step per block before the freed one, not a constant.

Freed blocks merge with the free blocks next to them. Free space at the end of the used data returns to the unallocated tail.

## Pinned blocks

A pinned block is a block that never moves. It is for code that keeps a pointer to the data: an interrupt, a DMA transfer, a kernel object.

```cpp
void* storage = heap.AllocatePinned(64, alignof(uint32_t));   // nullptr when refused
Memory::Handle pinned = heap.AllocatePinnedHandle(64, 8);     // the same block, with a handle
heap.Deallocate(pinned);                                      // refused: the block stays valid
heap.Reallocate(pinned, 128);                                 // refused: returns an invalid handle
heap.ReleasePinned(storage);                                  // the only way to free it
```

Rules:
- The alignment is a power of two, up to 64 bytes. Another value is refused with `nullptr` or an invalid handle.
- `Defragment()` and `DefragStep()` never move a pinned block. The defragmenter skips a pinned block: the free space in front of it stays where it is, and the blocks behind it can still move.
- `ReleasePinned()` ignores a pointer that is not a pinned block.
- A refused request for a pinned block calls the out-of-memory policy, as a movable request does.

Placement rule: a pinned block is placed by the same first fit as a movable block. A pinned block is not placed at an end of the arena (measurements below).

A program that calls only `Allocate`, `Deallocate` and `Reallocate` carries none of the aligned and pinned code. The default path and the pinned path are separate instantiations, and the pinned one exists only in a program that calls `AllocatePinned` or `AllocatePinnedHandle`.

## Interrupts

Allocation (`Allocate`, `Reallocate`, `AllocatePinned`, `AllocatePinnedHandle`) is not for an interrupt. Debug builds assert `!System::IsInterrupt()` on each of these calls, through `SystemAssert`. The check is compiled out with `NDEBUG`. A port provides `System::IsInterrupt()`.

## Containers

`Vector`, `String`, `PoolAllocator` and `SharedPtr` take their allocator explicitly. There is no default heap: a container with no allocator cannot grow, and its growing operations return false (`PushBack`, `Reserve`, `Assign`) or an empty result (`String` constructors and `operator+`).

The second template parameter names the allocator type. The default, `IAllocator`, takes any allocator through a pointer and calls it through a virtual table, which allows the choice at run time. A heap type (`Memory::Heap<>`) or a pool (`Memory::PoolAllocator<...>`) named as the type has its members called directly, with no virtual table and no adapter. `Memory::Heap` is not an `IAllocator`; wrap it in `AllocatorOf` to use it through the default form.

```cpp
Memory::Vector<int> values(&adapter);                                          // IAllocator, from AllocatorOf<Memory::Heap<>> adapter(heap)
Memory::Vector<int, Memory::Heap<>> fixed(&heap);                              // the heap type, called directly

Memory::String<Memory::Heap<>> text("hello", &heap);                           // String is a class template: Memory::String<> in a declaration
text.Append(" world");

Memory::SharedPtr<int, Memory::Heap<>> direct = Memory::AllocateShared<int, Memory::Heap<>>(&heap, 42);
```

`String` keeps its text as a `char` vector with a terminator. `String::ToView()` returns a `std::string_view`.

`SharedPtr` is compared with `std::shared_ptr`. `std::shared_ptr` allocates its control block from `operator new` (or from an allocator through `std::allocate_shared`), and the object it points to stays at the address it was given. `Memory::SharedPtr` keeps the object in a heap block and reaches it through a `Handle`, so the object follows a block that `Defragment()` moves. It takes the allocator it is given and no other. It is kept for that reason.

## Storage

`Utilities/Memory/Storage.h` holds the storage policies of a container. A container takes a policy as a template argument, with `Memory::Inside` as the default, and keeps the storage as a member:

```cpp
typename Policy::template Storage<ElementType, Capacity> storage;
```

A storage has `Ready()`, which is false when the storage has no memory, and `Data()`, which points to `Capacity` elements only while `Ready()` is true. A container refuses its writes while `Ready()` is false, and a container that is not ready holds no elements. `RingBuffer` is the container that uses the policies (see the Buffer page).

| Policy | Memory | Ready |
|---|---|---|
| `Memory::Inside` | an array of the object | always true |
| `Memory::FromHeap<heap>` | a pinned block of `heap`, taken in the constructor and released in the destructor | false when the heap has no block of that size |
| `Memory::External` | the span passed to the constructor | false when the span holds fewer than `Capacity` elements |

```cpp
alignas(8) uint8 arena[512];
Memory::Heap<> heap(arena, sizeof(arena));            // namespace scope: the policy needs a static object

void Fill() {
	Memory::FromHeap<heap>::Storage<uint32, 8> block; // 8 uint32 in a pinned block
	block.Data()[0] = 1;                              // the block does not move
}
```

Rules for `FromHeap`:
- The heap is a static object with external linkage. The heap object is constructed before a `FromHeap` storage is built and is destroyed after it, because the storage gives its block back to the heap in its destructor. A debug build asserts this, through the magic word that the heap sets in its constructor. A static storage in another translation unit may be constructed before the heap, so such a container is a function-local static, or it is created after the heap is initialised in `main`.
- A pinned block is aligned to `alignof(ElementType)` or more. `Data()` stays valid until the storage is destroyed. An interrupt or a DMA may keep the pointer.
- `Reallocate` and `Deallocate` refuse a pinned block, and only `ReleasePinned` frees it.
- A `FromHeap` storage cannot be copied, only moved. A move takes the block: the target holds it, and the source keeps no block and is not ready. A container that moves its storage must reset its own state too, as `RingBuffer` does.
- With `NDEBUG`, the magic word and the assert are compiled out.

Storage costs, Cortex-M0+ and M4, measured with a `RingBuffer<uint8, 16>` (see the Buffer page): the `Inside` ring is byte-equal to the ring without the storage parameter. The `FromHeap` ring adds the heap's allocator once (about 0.9 KB of text) and about 96 bytes of text for each further ring of another type. Per ring, the object is 12 bytes instead of 22 bytes, and the block takes a 12-byte header and its elements.

## Measurements

Object sizes, Cortex-M0+ and Cortex-M4F (`arm-none-eabi-g++` 15.2, `-std=c++23 -Os -fno-exceptions -fno-rtti`, `sizeof` values read from the compiled image; the same on both targets):

| Type | Size |
|---|---|
| `Heap<>` | 24 B with debug checks, 20 B with `NDEBUG` |
| `BlockHeader` | 12 B |
| `Handle` | 8 B |
| `Entry` | 16 B |

Header layout, Cortex-M0+ (layout arithmetic, not measured on a target):
- Option A, an 8-byte aligned header: 16 B of header per block, always.
- Option B, the chosen layout: a 12-byte header, and the data starts at the next 8-byte boundary. The data offset is 12 B when the header is at 4 mod 8 and 16 B when it is at 0 mod 8, so the header costs 12 B or 16 B per block.

Text of the linked image, the `text` column of `arm-none-eabi-size`, which includes the read-only data. Link line: `arm-none-eabi-g++ -std=c++23 -fno-exceptions -fno-rtti -Os -ffunction-sections -fdata-sections -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs`, with `-mcpu=cortex-m0plus -mthumb` for M0+ and `-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard` for M4F, and `-DNDEBUG` for the `NDEBUG` columns. Each program is a `main` with a `Heap<>` over a 512-byte static buffer, used only through the container. A stub defines `System::IsInterrupt()` and `System::Abort()`.

| Program | M0+ debug | M0+ `NDEBUG` | M4F debug | M4F `NDEBUG` |
|---|---|---|---|---|
| `Memory::Vector<int, Memory::Heap<>>`, 20 `PushBack` | 1956 B | 1928 B | 1864 B | 1836 B |
| `Memory::String<Memory::Heap<>>`, `"hello"`, then one `Append(" world")` | 2269 B | 2237 B | 2181 B | 2153 B |

The Vector program contains no instance of the aligned or pinned path. The debug build adds the interrupt assertion and the magic word, which is the difference between the debug and `NDEBUG` columns.

Placement of pinned blocks (prototype, host, 4096-byte arena, 50000 steps, 20 percent of the requests are pinned, two defragmentation steps per step; `Mean` is the mean over all steps):

| Live slots | Rule | Mean fragmentation | Mean largest free block | Movable refused (count) | Pinned refused (count) |
|---|---|---|---|---|---|
| 24 | skip (chosen) | 12.8 % | 2192 B | 0 | 0 |
| 24 | end of arena | 33.4 % | 1760 B | 0 | 0 |
| 40 | skip (chosen) | 11.7 % | 1056 B | 63 | 2 |
| 40 | end of arena | 28.8 % | 877 B | 84 | 0 |
| 60 | skip (chosen) | 9.1 % | 313 B | 3465 | 114 |
| 60 | end of arena | 13.5 % | 296 B | 3505 | 95 |

Text of the end-of-arena rule in the prototype is 560 B against 240 B for the chosen rule. The chosen rule keeps pinned blocks in the same first fit and lets the defragmenter skip them.

Measurement notes: the placement numbers come from a prototype with the same layout rules as the heap, not from the heap itself. Two seeds gave the same ordering at each load.
