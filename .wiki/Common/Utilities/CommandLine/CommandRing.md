# CommandRing

`Common/Utilities/CommandLine/CommandRing.h`: the input ring of a console. One producer, `Feed`, which may run in an interrupt, and one consumer, `Process`, in the main loop.

```cpp
template<std::size_t Size>
class CommandRing {
	explicit CommandRing(uint32 start = 0); // sets every position to start; the tests start near 2^32 to cross the wrap
	bool Push(uint8 byte);           // producer: false, and counted as lost, when the ring is full
	bool RefuseInterrupt();          // producer: records a refused Ctrl+C at the bytes accepted so far; false, and dropped, when two already wait
	bool Pop(uint8& byte);           // consumer: false when empty, or when the byte lies at the place of a due refused Ctrl+C
	bool InterruptDue();             // consumer: true once for each refused Ctrl+C whose place is reached, oldest first; call it before each Pop, and after a Pop that returns false
	bool InterruptWaiting();         // consumer: true while a refused Ctrl+C is not yet taken; a long command checks it on each pass
	void TakeWaitingInterrupt();     // consumer: takes the oldest waiting Ctrl+C and drops the bytes before its place
	bool DiscardTo(uint8 byte);      // consumer: drops the bytes up to and including the first byte before the oldest waiting press's place; false and nothing dropped when there is none (a long command's cancel)
	uint32 TakeLost();               // consumer: the bytes lost since the last call
};
```

`InputRing<N>` is the type that sets the size: `CommandLine` lists it among its types, and the ring is 32 bytes without it. The size must be a power of two.

---

## Ownership

- Every counter has one writer, and each side uses only loads and stores. There is no read-modify-write (`fetch_add`, `exchange`), because Cortex-M0 has no atomic read-modify-write instructions and would need library calls for them.
- The producer owns `written` (the bytes accepted), `lostTotal`, the refused Ctrl+C count (`interruptCount`) and the places of those presses (`interruptAt`). The consumer owns `consumed` and the count of presses it has taken (`interruptTaken`). `TakeLost` keeps its own copy of `lostTotal`, so the difference is the lost bytes since the last call.
- The ring is full when `written - consumed` reaches its size. Each side reads the other's counter with acquire, and writes its own with release. No side masks interrupts, so an interrupt never blocks.

## Positions and the wrap

- The positions (`written`, `consumed`, `interruptAt`) and the counts are `uint32` on every target, so the host runs the arithmetic of a 32-bit target. They wrap at 2^32.
- A byte's index is its position modulo the size. Only a power-of-two size divides 2^32, so each slot keeps its index across the wrap. That is why the size must be a power of two.
- Positions are compared only through `Reached(position, place)`, which is true when `position - place`, read as a signed 32-bit value, is not negative. Two positions of the ring are at most `Size` apart, and `Size` is at most 2^30, so that difference is exact. A ring of 2^31 bytes could reach a difference of 2^31, which reads as negative, so the bound is 2^30. No ordered comparison of two raw positions is left in the class. A distance, such as the fill `written - consumed`, is an unsigned difference.
- `Size` above 2^30 is a compile error (`a command line input ring holds at most 2^30 bytes`).
- The start position is for tests. `CommandRing<4> ring{0xFFFFFFFDu};` starts three positions short of the wrap, so a few bytes cross it.

## Refused Ctrl+C

A refused Ctrl+C acts at its place, like an accepted one: the bytes accepted before it are read first, and then it raises one Interrupt. Each press has its own place. Two refused presses that wait together at the same place raise two Interrupts, one after the other.

- `interruptAt` has two slots. Press `n` uses slot `n % 2`, and the place is the number of bytes accepted when the press is refused.
- At most two presses wait at once. A press refused while two wait is dropped: `RefuseInterrupt` returns false and records nothing, so that press raises no Interrupt. The console ignores the result. The byte itself is already counted as lost, like any byte the full ring refuses.
- `InterruptDue` raises the oldest waiting press when the consumer reaches its place, then the next one when its place is reached.
- `TakeWaitingInterrupt` takes the oldest waiting press at once, for a long command, and drops the bytes before that press's place; the bytes after it stay, and a newer press keeps its own place and fires at it. `InterruptWaiting` only reports whether a press waits.
- `DiscardTo` only drops bytes, and its search stops at the place of the oldest waiting press. An accepted Ctrl+C before that place drops the bytes up to it, so the console can cancel the command, and the press stays. A Ctrl+C after that place is not dropped, so that press raises its own Interrupt.
- `Pop` does not take a byte that lies at the place of a due press. It returns false there, and `InterruptDue` then raises the Interrupt before that byte. The consumer calls `InterruptDue` again after a `Pop` that returns false.
- Memory order: the producer stores the place (plain) before it stores the count (release). The consumer reads the count with acquire, so it also reads the place it needs. `Pop` reads the count after `written`. A byte at a place is stored after the count, so when `Pop` sees the byte it also sees the press. The consumer stores `interruptTaken` with release after it has read the slot, and the producer reads it with acquire. So the producer writes a slot again only after the consumer has finished reading it.
- A stale read of `interruptTaken` by the producer only makes it see more waiting presses than there are. Then it drops a press it could have kept. It never writes a slot that the consumer still reads, and it never gives a press a wrong place. The count of waiting presses is never negative, because the consumer takes only presses that the producer has recorded.

A `Size` of 0 is a compile error (`a command line input ring needs at least one byte`). A size that is not a power of two is a compile error (`a command line input ring needs a power-of-two size`).

---

## Why not RingBuffer.h

`Utilities/Buffer/RingBuffer.h` does not fit. Each of its operations masks interrupts, which `Feed` must not do from an interrupt, and it holds a `std::function`, which the console does not use. It also has no lost count.
