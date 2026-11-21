# Trigger

A latch for events that an interrupt raises and a task takes. Two classes: `Trigger` (set and take) and `EdgeTrigger` (samples a level and latches its rising edges).

## Table of Contents

- [Quick Start](#quick-start)
- [Trigger](#trigger-1)
- [EdgeTrigger](#edgetrigger)
- [API Reference](#api-reference)

## Quick Start

```cpp
#include <Utilities/Trigger/Trigger.h>
#include <Utilities/Trigger/EdgeTrigger.h>

// Set from an interrupt, taken in a task
Trigger dataReady;

void OnDataInterrupt() {
    dataReady.Set();
}

void Task() {
    if (dataReady.Take()) {
        ProcessData();
    }
}

// A rising edge of a level, sampled in the main loop
EdgeTrigger button;

void MainLoop() {
    button.Update(ReadButton());
    if (button.Take()) {
        OnPress();
    }
}
```

---

## Trigger

A one-bit latch. `Set()` raises it. `Take()` returns it and clears it in one step. `IsSet()` reads it without clearing.

- `Set()` is safe from an interrupt. On the critical-section path it is safe only where `System::CriticalSection` is: on ESP32 that is task-level, so call `Set()` from a task there.
- A `Set()` that arrives during a `Take()` is not lost. It stays for the next `Take()`.
- Two `Set()` calls before one `Take()` make one event. A latch holds a yes or no, not a count.
- A latch has one owner, so it cannot be copied.

On targets where GCC reports `bool` atomics as always lock-free (checked for Cortex-M4 and x86) the latch is a `std::atomic<bool>`. On Cortex-M0+, which has no exclusive load and store, each operation runs in a `System::CriticalSection`. The choice is made at compile time, and the code is the same.

```cpp
Trigger error;

error.Set();
if (error.IsSet()) {            // true, nothing cleared
    // ...
}
if (error.Take()) {             // true, and the latch is clear
    // ...
}
if (error.Take()) {             // false
}
```

---

## EdgeTrigger

Latches a rising edge (false to true) of a level that the caller samples. `Update(sample)` takes one sample. A change from `false` to `true` since the previous sample sets the latch.

- `IsSet()` and `GetState()` never change anything. Only `Take()` clears the latch.
- Call `Update()` from one context only.
- A pulse that rises and falls between two samples is not seen. Sample often enough for the signal, or call `Trigger::Set()` from the edge interrupt itself.

```cpp
EdgeTrigger overCurrent;

// In the loop, every 1 ms
overCurrent.Update(ReadOverCurrentPin());
if (overCurrent.Take()) {
    ShutDown();
}
```

---

## API Reference

### Trigger (`BasicTrigger<lockFree>`, alias `Trigger`)

| Member | Description |
|---|---|
| `Trigger()` | Default constructor. The latch is clear |
| `Trigger(const Trigger&)`, `operator=` | Deleted. The latch has one owner |
| `void Set()` | Raises the latch. Safe from an interrupt, with the target note above |
| `[[nodiscard]] bool Take()` | Returns `true` when the latch is raised, and clears it |
| `bool IsSet() const` | Returns the latch without clearing it |

`BasicTrigger<true>` uses `std::atomic<bool>`. `BasicTrigger<false>` uses a critical section. `Trigger` is `BasicTrigger<std::atomic<bool>::is_always_lock_free>`.

### EdgeTrigger

| Member | Description |
|---|---|
| `EdgeTrigger()` | Default constructor. The level and the latch are clear |
| `void Update(bool sample)` | Takes one sample. A rising edge since the last sample sets the latch |
| `[[nodiscard]] bool Take()` | Returns `true` when a rising edge is latched, and clears it |
| `bool IsSet() const` | Returns the latch without clearing it |
| `bool GetState() const` | Returns the level of the last sample |
