# CommandRing

`Common/Utilities/CommandLine/CommandRing.h`: the input ring of a console. One producer, `Feed`, which may run in an interrupt, and one consumer, `Process`, in the main loop.

```cpp
template<std::size_t Size>
class CommandRing {
	bool Push(uint8 byte);           // producer: false, and counted as lost, when the ring is full
	void RefuseInterrupt();          // producer: a refused Ctrl+C takes its place after the bytes accepted so far
	bool Pop(uint8& byte);           // consumer: false when empty
	bool InterruptDue();             // consumer: true once when a refused Ctrl+C's place is reached; call it before each Pop
	uint32 TakeLost();               // consumer: the bytes lost since the last call
};
```

`InputRing<N>` is the type that sets the size: `CommandLine` lists it among its types, and the ring is 32 bytes without it.

---

## Ownership

- Every counter has one writer, and each side uses only loads and stores. There is no read-modify-write (`fetch_add`, `exchange`), because Cortex-M0 has no atomic read-modify-write instructions and would need library calls for them.
- The producer owns `written` (the bytes accepted, counted up without wrapping), `lostTotal` and the refused Ctrl+C's place and count. The consumer owns `consumed`. `TakeLost` keeps its own copy of `lostTotal`, so the difference is the lost bytes since the last call.
- The ring is full when `written - consumed` reaches its size. Each side reads the other's counter with acquire, and writes its own with release. No side masks interrupts, so an interrupt never blocks.

A `Size` of 0 is a compile error (`a command line input ring needs at least one byte`).

---

## Why not RingBuffer.h

`Utilities/Buffer/RingBuffer.h` does not fit. Each of its operations masks interrupts, which `Feed` must not do from an interrupt, and it holds a `std::function`, which the console does not use. It also has no lost count.
