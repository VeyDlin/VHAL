# VSTD — Embedded C++ Standard Library

Header-only C++20 standard library replacement for embedded systems (Cortex-M, ESP32). All heap-backed types use a **handle-based allocator** with optional defragmentation — pointers remain valid after memory compaction.

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

All heap allocations return a `Handle` — an indirect reference through an entry table. The heap can relocate data blocks during defragmentation without invalidating handles.

```
Buffer layout:
[ data blocks →                    ← handle table ]
  BlockHeader+data  BlockHeader+data     Entry Entry
```

```cpp
VSTD::Handle h = heap.Allocate(64);
void* ptr = h.Data();       // resolve current pointer
std::size_t sz = h.Size();  // allocation size
bool ok = h.IsValid();      // check if alive
```

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

**Defragmentation policies** — template parameter controls when/how defrag runs:
```cpp
VSTD::Heap<VSTD::NoDefrag>              heap(buf);   // no defrag (default)
VSTD::Heap<VSTD::IncrementalDefrag<1>>  heap(buf);   // 1 step per Defragment() call
VSTD::Heap<VSTD::BatchDefrag<4>>        heap(buf);   // 4 steps per call
VSTD::Heap<VSTD::ThresholdDefrag<30>>   heap(buf);   // defrag when fragmentation ≥ 30%
```

```cpp
heap.Defragment();       // run defrag policy
heap.DefragStep();       // single compaction step (manual)
```

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

Fixed-size block allocator. O(1) alloc/dealloc via free list. Implements `IAllocator`.

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

### String

Null-terminated dynamic string. Built on `Vector<char>`.

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

s.Substr(0, 5);            // "hello"
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
VSTD::Variant<int, float, VSTD::String> v;

v.Set<int>(42);
v.Is<int>();           // true
v.Get<int>();          // 42

v.Set<float>(3.14f);
v.Index();             // 1 (index of float in type list)

// Visit pattern
v.Visit([](auto& val) {
    // val is the active type
});
```

### UniquePtr

Exclusive ownership smart pointer. Heap-backed through handles.

```cpp
auto ptr = VSTD::MakeUnique<int>(42);
auto ptr2 = VSTD::MakeUnique<MyStruct>(&pool, arg1, arg2);  // with allocator

*ptr;            // 42
ptr->Method();
ptr.Get();       // raw pointer (or nullptr)
bool ok = ptr;   // true if valid

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

Type-erased callable wrapper. Small-buffer optimization (32 bytes) with heap fallback.

```cpp
VSTD::Function<int(int, int)> fn = [](int a, int b) { return a + b; };
int result = fn(2, 3);  // 5

bool callable = fn;     // true if holds a callable

// Lambdas with captures
int x = 10;
VSTD::Function<int()> fn2 = [x]() { return x * 2; };
```

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
bool ok = d2;     // true if bound
d2.Reset();       // unbind
```

## Core Utilities

```cpp
VSTD::Move(value);          // cast to rvalue reference
VSTD::Forward<T>(value);    // perfect forwarding
VSTD::Swap(a, b);           // swap two values
```
