# WaitCondition

A header-only wait for a condition, with a timeout. An adapter uses it where it waits for a flag, a state or a peripheral.

---

## Table of Contents

- [Quick Start](#quick-start)
- [WaitFor](#waitfor)
- [Policies](#policies)
- [Example: an adapter wait](#example-an-adapter-wait)
- [Limits](#limits)

---

## Quick Start

```cpp
#include <Utilities/WaitCondition/WaitCondition.h>

// Wait at most 2 ms for the transmit-complete flag
ResultStatus status = WaitCondition::WaitFor([this]() -> bool {
    return LL_USART_IsActiveFlag_TC(uartHandle);
}, 2);

if (status != ResultStatus::ok) {
    // ResultStatus::timeout
}
```

---

## WaitFor

```cpp
template<typename Policy = NoYield, typename Predicate>
ResultStatus WaitCondition::WaitFor(Predicate&& predicate, uint32 timeoutMs);
```

Polls `predicate()` until it returns `true` (returns `ResultStatus::ok`) or until `timeoutMs` milliseconds have passed while it is still `false` (returns `ResultStatus::timeout`).

- The predicate runs first. A condition that is already true costs one call and no clock read.
- The timeout is measured with `System::GetTick32()`. A tick is one period of `System::TickHandler`. The application calls it once per millisecond, so there a tick is one millisecond.
- The result is `timeout` only when the last predicate call returned `false`. A condition that is true at that call returns `ok`, even when that call reaches the limit.
- `timeoutMs = 0` polls the predicate twice (on entry and before the timeout check) and then returns `timeout` if it is still false.
- The predicate and the policy are template arguments. A lambda inlines. Nothing is allocated, and no `std::function` is built.

| Parameter   | Type       | Description                                                  |
|-------------|------------|--------------------------------------------------------------|
| `predicate` | callable   | Returns `true` when the wait is over. Taken by forwarding reference |
| `timeoutMs` | `uint32`   | Longest wait in milliseconds                                  |

**Returns:** `ResultStatus::ok` when the predicate became `true`, `ResultStatus::timeout` otherwise.

---

## Policies

The policy decides what happens between two polls. It is the first template argument.

| Policy | Between polls | Use it for |
|--------|---------------|------------|
| `NoYield` (default) | Nothing. The loop polls at once | A flag that a peripheral sets within microseconds |
| `RtosYield` | Calls `System::rtosDelayMsHandle(1)` when it is set, which yields to the RTOS for one millisecond. With no handle it polls like `NoYield` | A wait that can take milliseconds, under an RTOS |

```cpp
// A long wait under an RTOS: the other tasks keep running
ResultStatus status = WaitCondition::WaitFor<WaitCondition::RtosYield>([this]() -> bool {
    return state == ResultStatus::ready;
}, 1000);
```

---

## Example: an adapter wait

The base `I2CAdapter::Await()` waits for the state an interrupt sets. It uses the timeout from `SetTimeout`:

```cpp
virtual ResultStatus Await() {
    ResultStatus waitStatus = WaitCondition::WaitFor([this]() -> bool {
        return state == ResultStatus::ready || state == ResultStatus::error;
    }, timeout);

    if (waitStatus != ResultStatus::ok) {
        return waitStatus;   // timeout
    }

    return state == ResultStatus::ready ? ResultStatus::ok : ResultStatus::error;
}
```

`state` is `volatile`, because an interrupt writes it. A plain member could be read once and kept in a register.

---

## Limits

- **Clock.** The timeout is a count of ticks of `System::GetTick32()`, which is 32 bits wide and wraps about every 49.7 days. The wait compares with unsigned subtraction, so it stays correct across the wrap for any `uint32` timeout.
- **Frozen tick.** The tick moves when the application calls `System::TickHandler`, for example from the SysTick interrupt. Inside a critical section, or with the interrupts masked, it stops, and the wait cannot time out. Never wait there. A predicate that never becomes `true` then hangs, as it would with no timeout at all.
- **Cost.** One call to the predicate and one `System::GetTick32()` read per poll. `GetTick32()` is inline: one aligned 32-bit volatile load, with no call and no critical section.
