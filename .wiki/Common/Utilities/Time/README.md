# Ticks

Comparisons of free-running tick counters that hold across a wrap, and the millisecond split that a timeval needs. The code is in `Common/Utilities/Time/Ticks.h`, in the namespace `Ticks`.

A 32-bit counter of microseconds wraps about every 71 minutes, and a 32-bit counter of milliseconds about every 49 days. A plain `<` or `>=` between two such values is wrong after the wrap. The `uint32` forms below compare the difference of the two values, so they stay right across the wrap. The `uint64` forms compare the values as they are.

| Function | Result |
|---|---|
| `Ticks::Reached(now, due)` | `true` when the counter has reached `due`; the two values must be less than 2^31 apart |
| `Ticks::Elapsed(now, since, interval)` | `true` when `interval` has passed since `since`; right across a wrap for any interval below 2^32 |
| `Ticks::SplitMs(ms)` | a `Ticks::Split` with the whole `seconds` of `ms` and the `microseconds` of the rest |
| `Ticks::usPerMs` | the microseconds in a millisecond, `1000` |

The `uint64` overloads of `Reached` and `Elapsed` take the same arguments and compare them without a wrap. `Elapsed` gives `false` when `since` is after the counter.

## Reached

`Reached` is the test for a deadline. The deadline is stored as a tick, and the check is made each time the counter is read:

```cpp
#include <Utilities/Time/Ticks.h>

uint32 deadline = 0xFFFFFFF0u;

bool late = Ticks::Reached(0x00000010u, deadline);   // true: 0x10 is 32 ticks after the deadline across the wrap
bool early = Ticks::Reached(0xFFFFFFF0u, 0x00000010u); // false: the deadline is 32 ticks ahead
```

## Elapsed

`Elapsed` is the test for a span. The start is stored when the span begins:

```cpp
#include <Utilities/Time/Ticks.h>

uint32 start = 0xFFFFFFFCu;
uint32 nowTick = 0x00000004u;

bool done = Ticks::Elapsed(nowTick, start, 8);   // true: 8 ticks have passed across the wrap
bool notYet = Ticks::Elapsed(nowTick, start, 9); // false: 9 ticks have not passed
```

The start must be at or before the counter. A start ahead of the counter gives a difference near 2^32, so `Elapsed` gives `true`.

## 64-bit forms

A `uint64` counter does not wrap in practice. The same functions take `uint64` arguments and compare them as given:

```cpp
#include <Utilities/Time/Ticks.h>

uint64 totalUs = 5000000;

bool reached = Ticks::Reached(totalUs, uint64{4000000}); // true
bool elapsed = Ticks::Elapsed(totalUs, uint64{4000000}, uint64{1000000}); // true
```

The 64-bit forms take two `uint64` arguments. Pass `uint64` for both arguments: a call that mixes a `uint64` with a `uint32` or an `int` is not a 64-bit call, and it reaches the 32-bit form, which narrows the `uint64` argument to 32 bits.

## Split

`SplitMs` gives the whole seconds and the remaining microseconds. Each port builds its own `timeval` from the two fields:

```cpp
#include <Utilities/Time/Ticks.h>

Ticks::Split parts = Ticks::SplitMs(2500);  // seconds 2, microseconds 500000

uint32 delayUs = Ticks::usPerMs * 20;       // a 20 ms delay in microseconds
```

## Choosing a form

Use the `uint32` forms for a counter that wraps, such as a hardware tick or a 32-bit millisecond count. Use the `uint64` forms for a counter that does not wrap.
