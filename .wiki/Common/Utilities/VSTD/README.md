# VSTD — Embedded C++ Standard Library

Header-only C++20 standard library replacement for embedded systems (Cortex-M, ESP32). All heap-backed types use a **handle-based allocator** with optional defragmentation. Only a `Handle` stays valid across defragmentation; raw pointers do not (see [Handle-Based Indirection](#handle-based-indirection)).

## Quick Start

```cpp
#include <Utilities/VSTD/VSTD.h>

// 1. Create a heap from a static buffer
uint8_t buffer[4096];
VSTD::Heap heap(buffer, sizeof(buffer));
VSTD::SetDefaultHeap(&heap);

// 2. Use containers — they allocate from the default heap
VSTD::Vector<int> vec;
vec.PushBack(1);
vec.PushBack(2);
vec.PushBack(3);

VSTD::String str("hello");
str.Append(" world");

// 3. Smart pointers work through handles too
auto ptr = VSTD::MakeUnique<int>(42);
auto shared = VSTD::MakeShared<int>(100);
```

## Memory System

### Handle-Based Indirection

All heap allocations return a `Handle` — an indirect reference through an entry table. The heap can relocate data blocks during defragmentation, and a `Handle` follows the move.

```
Buffer layout:
[ data blocks →                    ← handle table ]
  BlockHeader+data  BlockHeader+data     Entry Entry
```

```cpp
VSTD::Handle h = heap.Allocate(64);
void* ptr = h.Data();       // resolve current pointer
std::size_t sz = h.Size();  // allocation size, rounded up to a multiple of 8 bytes
bool ok = h.IsValid();      // check if alive
```

**Validity of pointers.** A pointer from `Handle::Data()`, and any reference or iterator into a block, is valid only until the next call that can move or free that block: `Defragment()`, `DefragStep()`, `Reallocate()`, `Deallocate()`, or a growing operation of a container. Keep the `Handle` and call `Data()` again after any of these. Only the `Handle` stays valid across `Defragment()`.

**Stale handles.** Each entry carries a generation that changes whenever the slot is reused. A copy of a `Handle` taken before its block was freed stays invalid, even after another block takes the same slot:

```cpp
VSTD::Handle owner = heap.Allocate(32);
VSTD::Handle stale = owner;     // a copy
heap.Deallocate(owner);
VSTD::Handle reuser = heap.Allocate(32);

stale.IsValid();                // false, even if reuser took the same slot
heap.Deallocate(stale);         // does nothing; reuser is still valid
stale.Invalidate();             // only clears stale; reuser is still valid
```

`Handle::Invalidate()` clears only the copy it is called on. The block stays allocated, and other copies of the handle stay valid. Only `Deallocate()` frees a block. `Data()` and `Size()` of an invalid handle return `nullptr` and `0`, and `GetEntry()` returns `nullptr` for an invalid handle.

The generation has 32 bits. After a slot has been brought into use 2^32 times, an old copy can match it again, so a copy kept across that many reuses of one slot can be wrongly valid.

### Heap

```cpp
// From a static buffer
uint8_t buf[8192];
VSTD::Heap heap(buf, sizeof(buf));

// Or from an array reference
uint8_t buf[8192];
VSTD::Heap heap(buf);

// Or with internal allocation (new[])
VSTD::Heap heap(8192);
```

The buffer constructors start the heap at the first address that aligns entries, so a buffer that is not aligned, or whose size is not a multiple of the entry alignment, loses a few bytes at either end. `TotalBytes()` reports the usable capacity.

**Global operator new.** The constructor with an internal buffer (`Heap heap(8192)`) allocates with the global `operator new[]` and frees with `operator delete[]`. A port must provide both. The constructors that take a buffer need no global operator. Nothing else in VSTD calls `new` except placement new, which needs only `<new>`.

**Allocator interface:**
```cpp
Handle h = heap.Allocate(128);           // allocate 128 bytes
Handle h2 = heap.Reallocate(h, 256);     // grow (copies data)
heap.Deallocate(h2);                     // free
```

**OOM callback:**
```cpp
heap.OnOutOfMemory([]() {
    // defragment, log, or panic
});
```

**Merging free blocks.** When a block is freed, it merges with a free block before it and with free blocks after it. A free block at the end of the used region returns its space to the unallocated tail. So a request can use the whole of two adjacent freed blocks without any defragmentation.

**Defragmentation policies** — template parameter controls when/how defrag runs. Defragmentation runs only when the application calls `Defragment()`; there is no background collector:
```cpp
VSTD::Heap<VSTD::NoDefrag>              heap(buf);   // no defrag (default): Defragment() moves nothing
VSTD::Heap<VSTD::IncrementalDefrag<1>>  heap(buf);   // 1 step per Defragment() call
VSTD::Heap<VSTD::BatchDefrag<4>>        heap(buf);   // 4 steps per call
VSTD::Heap<VSTD::ThresholdDefrag<30>>   heap(buf);   // defrag when fragmentation ≥ 30%
```

```cpp
heap.Defragment();       // run defrag policy
heap.DefragStep();       // single compaction step (manual)
```

A step moves at most one used block down over a free block. Every block that moves gets a new address, so pointers and references into it are invalid afterwards. Its `Handle` keeps the data:

```cpp
VSTD::Heap<VSTD::IncrementalDefrag<4>> heap(buf);
VSTD::Handle a = heap.Allocate(64);
VSTD::Handle b = heap.Allocate(64);
VSTD::Handle c = heap.Allocate(64);
uint8_t* raw = static_cast<uint8_t*>(c.Data());

heap.Deallocate(a);
heap.Deallocate(b);
heap.Defragment();       // c moved down over the free space

c.Data() != raw;         // true: the raw pointer is stale
c.Data();                // the current address of c, with its contents
```

An allocation can still fail when the free space is split into several blocks that are not next to each other. `Defragment()` is the tool for that case.

Each call takes the lock once. `Reallocate()` allocates, copies and frees under that one lock, so `Defragment()` cannot move the old block between the steps.

**Lock policy** — second template parameter for thread safety:
```cpp
struct MyLock {
    static void Lock()   { /* acquire mutex */ }
    static void Unlock() { /* release mutex */ }
};

VSTD::Heap<VSTD::NoDefrag, MyLock> heap(buf);
```

**Stats:**
```cpp
heap.UsedBytes();             // bytes in live blocks
heap.FreeBytes();             // free bytes (unallocated + free blocks)
heap.TotalBytes();            // total buffer size
heap.FragmentationPercent();  // free bytes within data region, 0–100
```

### PoolAllocator

Fixed-size block allocator. O(1) alloc/dealloc via free list. Implements `IAllocator`. `BlockSize` is the minimum usable size of a block: blocks are laid out at a stride rounded up to pointer alignment, so any `BlockSize` of at least `sizeof(void*)` works. `GetBlockSize()` returns the requested `BlockSize`.

```cpp
VSTD::PoolAllocator<64, 16> pool;   // 16 blocks of 64 bytes each

Handle h = pool.Allocate(32);       // uses one block (max 64 bytes)
pool.Deallocate(h);

pool.AllocatedCount();   // live blocks
pool.FreeCount();        // available blocks
pool.TotalBlocks();      // 16
pool.GetBlockSize();     // 64
```

### Default Heap

Containers use the default heap when no allocator is specified:

```cpp
VSTD::SetDefaultHeap(&heap);
VSTD::IAllocator* alloc = VSTD::GetDefaultHeap();

// Containers use it automatically
VSTD::Vector<int> v;              // uses default heap
VSTD::Vector<int> v2(&pool);      // uses specific allocator
```

A container remembers the allocator that holds its block. It frees the block to that allocator, even if the default heap changes later.

**No default heap.** When no allocator is given and no default heap is set, containers do not crash. Every growing operation returns its failure value and leaves the container empty or unchanged, as for an allocator that refuses memory. `Vector` and `String` behave this way.

## Empty Containers

Rule for every container that can be empty: `Front()` and `Back()` return a pointer to the first or last element, and return `nullptr` when the container is empty. The rule applies to `Vector`, `Span` and `StringView`. `Array` with a size greater than zero is never empty, so its `Front()` and `Back()` return references, and `Array<T, 0>` has neither.

`operator[]` does not check its index on any container.

```cpp
VSTD::Vector<int> v;
if (int* first = v.Front()) {    // nullptr when empty
    // use *first
}
```

## Containers

### Array

Stack-allocated fixed-size array.

```cpp
VSTD::Array<int, 4> arr;
arr[0] = 10;
arr.Fill(0);

arr.Size();    // 4
arr.Front();   // first element
arr.Back();    // last element
arr.Data();    // raw pointer

for (auto x : arr) { /* ... */ }
```

### Span

Non-owning view over contiguous data. Works with arrays, vectors, raw pointers.

```cpp
int raw[4] = {1, 2, 3, 4};
VSTD::Span<int> s(raw);             // from C array
VSTD::Span<int> s2(ptr, 4);         // from pointer + size
VSTD::Span<int> s3(vec);            // from any container with Data()/Size()

s.Subspan(1, 2);   // elements [1..2]
s.First(2);        // first 2 elements
s.Last(2);         // last 2 elements
```

### Vector

Dynamic array. Heap-backed with growth factor 2x.

```cpp
VSTD::Vector<int> v;
v.PushBack(1);
v.EmplaceBack(2);
v.PopBack();

v.Insert(0, 99);        // insert at index
v.Erase(0);             // erase at index
v.EraseRange(1, 3);     // erase 3 elements starting at index 1

v.Find(42);             // returns index or (size_t)-1
v.Contains(42);         // returns bool

v.Resize(10);           // grow/shrink (default-constructs new elements)
v.Reserve(100);         // preallocate capacity
v.Clear();              // destroy all elements

v.Size();
v.Capacity();
v.Empty();
v.Data();               // raw pointer
v.Front();
v.Back();
v[0];

for (auto& x : v) { /* ... */ }
```

**Allocation failure.** Operations that may grow the buffer report failure instead of writing past it. When the allocator refuses memory the vector is left unchanged: same size, capacity and elements.

| Operation | Returns |
|-----------|---------|
| `PushBack(value)` | `bool`, `false` when the allocator refuses memory |
| `EmplaceBack(args...)` | `T*` to the new element, `nullptr` when the allocator refuses memory |
| `Insert(index, value)` | `bool`; `false` for an index past the end, or when the allocator refuses memory |
| `Reserve(n)` | `bool`; `false` when the allocator refuses memory |
| `Resize(n)` | `bool`; `false` when growing is refused |

```cpp
if (!v.PushBack(42)) {
    // allocator out of memory: v still holds its previous elements
}
```

`EmplaceBack` used to return `T&`; code that chained a member access on the result must now check the pointer.

`PushBack(T&&)` and `EmplaceBack(args...)` accept a value that lies in the vector's own buffer. When the buffer must grow, the value is moved or built out first, because growing frees the old buffer. If the growth is then refused, a `PushBack(T&&)` argument that lay in the buffer has been moved from.

**Copying.** The copy constructor cannot report failure: when no allocator is available, or the allocator refuses memory, the new vector is empty. Copy assignment (`=`) also cannot report, and keeps the old contents on failure. For a checked copy use `Assign`:

```cpp
bool Assign(const Vector& other);   // false, with this vector unchanged, when the allocator refuses memory
```

`Assign` allocates the new buffer before it touches this vector, and adopts the allocator of `other`.

### String

Null-terminated dynamic string. Built on `Vector<char>`. The text is always terminated, also after a failed call.

```cpp
VSTD::String s("hello");
s.Append(" world");
s.Append('!');

s.Find('o');               // 4
s.Find("world");           // 6
s.Find('o', 5);            // 7 (search from index 5)
s.Contains("world");       // true
s.StartsWith("hello");     // true
s.EndsWith("world!");      // true

VSTD::String head;
s.Substr(0, head, 5);      // head == "hello"; returns bool
s.Insert(5, " beautiful");
s.Erase(0, 6);             // remove first 6 chars

s.Length();
s.Empty();
s.Data();                  // const char*, null-terminated
s.Clear();
s.ToView();                // returns StringView

// Comparison
s == "hello";
s != other;

// Concatenation
auto c = s + " suffix";
```

**Allocation failure.** Every operation that can allocate returns `bool`, and on `false` the string is unchanged (same text, still terminated):

| Operation | Returns |
|-----------|---------|
| `Append(const char*)`, `Append(char)`, `Append(const String&)` | `bool`; `false` when the allocator refuses memory |
| `Insert(index, const char*)`, `Insert(index, char)` | `bool`; `false` for an index past the end, or when the allocator refuses memory |
| `Substr(pos, out, count = Npos)` | `bool`; `out` receives the substring. `false`, with `out` unchanged, when the default heap refuses memory |
| `Assign(const String&)` | `bool`; `false` with this string unchanged |

Constructors and `operator=` cannot report failure. A constructor that is refused leaves the string empty. `operator=` keeps the old text; use `Assign` to learn whether it happened. `operator+` returns an empty string when memory runs out, so use `Append` where the difference matters.

`Append` and `Insert` taking a source that lies inside the string itself first copy that source, using the default heap.

```cpp
if (!s.Append(" more")) {
    // allocator out of memory: s still reads "hello world!"
}
```

### StringView

Non-owning view into a string. No allocation.

```cpp
VSTD::StringView sv("hello world");
VSTD::StringView sv2(ptr, len);

sv.Substr(6, 5);        // "world"
sv.First(5);             // "hello"
sv.Last(5);              // "world"

sv.Find('o');            // 4
sv.Find("world");        // 6
sv.Contains('o');        // true
sv.StartsWith("hello");  // true
sv.EndsWith("world");    // true

sv.RemovePrefix(6);      // mutates view: "world"
sv.RemoveSuffix(2);      // mutates view: "wor"

sv.Length();
sv.Empty();
sv.Data();
sv[0];

for (char c : sv) { /* ... */ }
```

## Utilities

### Optional

Stack-only optional value.

```cpp
VSTD::Optional<int> opt;
VSTD::Optional<int> opt2(42);
VSTD::Optional<int> opt3 = VSTD::Nullopt;

opt.HasValue();       // false
opt2.Value();         // 42
opt2.ValueOr(0);      // 42
opt.ValueOr(0);       // 0

*opt2;                // 42
opt2->Method();       // for struct/class types

opt.Emplace(10);      // construct in-place
opt.Reset();          // destroy value
```

### Variant

Type-safe tagged union. Stack-only.

```cpp
VSTD::Variant<int, float, VSTD::String> v;   // holds int 0 by default

v = 42;
VSTD::HoldsAlternative<int>(v);   // true
VSTD::Get<int>(v);                // 42

v.Emplace<float>(3.14f);
v.Index();                        // 1 (index of float in type list)

// Visit pattern
VSTD::Visit([](auto& val) {
    // val is the active type
}, v);
```

### UniquePtr

Exclusive ownership smart pointer. Heap-backed through handles.

```cpp
auto ptr = VSTD::MakeUnique<int>(42);
auto ptr2 = VSTD::MakeUnique<MyStruct>(static_cast<VSTD::IAllocator*>(&pool), arg1, arg2);  // with allocator
auto ptr3 = VSTD::MakeUnique<MyStruct>(&pool, arg1, arg2);  // derived allocator pointers (e.g. PoolAllocator*) also select this overload

*ptr;            // 42
ptr->Method();
ptr.Get();       // raw pointer (or nullptr)
bool ok = static_cast<bool>(ptr);   // true if valid

ptr.Reset();     // destroy and deallocate
Handle h = ptr.Release();  // release ownership, return handle
```

### SharedPtr

Reference-counted smart pointer. Heap-backed through handles.

```cpp
auto a = VSTD::MakeShared<int>(42);
auto b = a;                           // refcount = 2

a.UseCount();    // 2
a.IsUnique();    // false

*a;              // 42
a->Method();
a.Get();         // raw pointer

a.Reset();       // decrement refcount
// b still valid, refcount = 1

// With specific allocator
auto c = VSTD::AllocateShared<int>(&pool, 99);
```

### Function

Type-erased callable wrapper. Small-buffer optimization (32 bytes) with heap fallback. The callable is stored in the object or in a block, and nothing stores its address. So a `Function` moves correctly when a container grows or the heap defragments. The callable itself must tolerate a byte-for-byte copy.

```cpp
VSTD::Function<int(int, int)> fn = [](int a, int b) { return a + b; };
int result = fn(2, 3);  // 5

bool callable = static_cast<bool>(fn);   // true if holds a callable

// Lambdas with captures
int x = 10;
VSTD::Function<int()> fn2 = [x]() { return x * 2; };

// A block for a large callable comes from a given allocator, and is freed to it
VSTD::Function<int()> fn3(&pool, [big]() { return big[0]; });
```

- A callable that does not fit in the small buffer uses the default heap, or the allocator given to the constructor. The allocator is remembered and used for the free.
- If that allocator refuses the block, the `Function` is empty. `static_cast<bool>` reports it.
- Calling an empty `Function` returns a value-initialized result (`0` for `int`), and does nothing for `void`. So the result type must be default constructible and not a reference. Any other result type makes a call to the `Function` fail to compile.
- A callable must be aligned no more than `std::max_align_t` to fit the small buffer, and no more than a pointer to be stored in a block. An over-aligned callable fails to compile.

### Delegate

Zero-allocation callback. Stores a function pointer + void* context. No heap, no SBO.

```cpp
// Free function
VSTD::Delegate<int(int)> d = VSTD::Delegate<int(int)>::Bind<&MyFunc>();

// Member function
MyClass obj;
auto d2 = VSTD::Delegate<void()>::Bind<MyClass, &MyClass::DoWork>(&obj);

// Const member function
auto d3 = VSTD::Delegate<int()>::Bind<MyClass, &MyClass::GetValue>(&obj);

// Raw function pointer + context
auto d4 = VSTD::Delegate<void()>::FromRaw(myStub, context);

d2();             // call
bool ok = static_cast<bool>(d2);   // true if bound
d2.Reset();       // unbind
```

## Core Utilities

```cpp
VSTD::Move(value);          // cast to rvalue reference
VSTD::Forward<T>(value);    // perfect forwarding
VSTD::Swap(a, b);           // swap two values
```

## Port Requirements

VSTD is header-only. A port needs:

- `<string.h>`: `memcpy`, `memmove`, `memcmp`, `strlen`, `strcmp`.
- The standard header `<utility>` (`Vector`).
- The global `operator new[]` and `operator delete[]`, but only for the `Heap` constructor that takes a size. Heaps over a buffer you own do not use them.
- The standard headers `<new>`, `<cstddef>`, `<cstdint>` and `<type_traits>`. The placement `new` in `<new>` is the only `new` expression the containers use.

Nothing else from the platform is used. The heap takes no lock unless a lock policy is given, so it is not interrupt-safe without one.
