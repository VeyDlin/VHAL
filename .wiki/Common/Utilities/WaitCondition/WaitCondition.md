# WaitCondition

Polling-based conditional wait utilities with timeout support for embedded systems (VHAL).

---

## Table of Contents

- [Quick Start](#quick-start)
- [API Reference](#api-reference)
  - [Wait](#wait)
  - [WaitDouble](#waitdouble)
- [Usage Examples](#usage-examples)

---

## Quick Start

```cpp
#include <Utilities/WaitCondition/WaitCondition.h>

// Block until a flag is set, with a 500-tick timeout
bool ok = WaitCondition::Wait(
    []() { return myPeripheral.IsReady(); },
    500
);

if (!ok) {
    // Timed out
}
```

---

## API Reference

### Wait

```cpp
bool WaitCondition::Wait(std::function<bool()> condition, uint32 timeout);
```

Polls `condition()` in a busy loop until it returns `true` or the elapsed time since entry exceeds `timeout` ticks (as measured by `System::GetTick()`).

| Parameter   | Type                      | Description                                       |
|-------------|---------------------------|---------------------------------------------------|
| `condition` | `std::function<bool()>`   | Callable that returns `true` when the wait is over |
| `timeout`   | `uint32`                  | Maximum number of ticks to wait                    |

**Returns:** `true` if `condition()` became `true` before the timeout; `false` otherwise.

---

### WaitDouble

```cpp
bool WaitCondition::WaitDouble(
    std::function<bool()> condition,
    std::function<bool()> mandatoryCondition,
    uint32 timeout
);
```

Same as `Wait`, but adds a **mandatory condition** that is checked on every iteration. If `mandatoryCondition()` returns `false` at any point, the function exits immediately with `false` -- regardless of the timeout.

| Parameter            | Type                      | Description                                                  |
|----------------------|---------------------------|--------------------------------------------------------------|
| `condition`          | `std::function<bool()>`   | Callable that returns `true` when the wait is over            |
| `mandatoryCondition` | `std::function<bool()>`   | Invariant that must stay `true`; violation causes early abort |
| `timeout`            | `uint32`                  | Maximum number of ticks to wait                               |

**Returns:** `true` if `condition()` became `true` while `mandatoryCondition()` stayed `true` and before the timeout; `false` otherwise.

---

## Usage Examples

### Waiting for a peripheral flag

```cpp
bool ok = WaitCondition::Wait(
    []() { return UART_GetFlag(UART1, UART_FLAG_TC); },
    1000
);
```

### Waiting with a safety guard (WaitDouble)

Wait for a transfer to complete, but abort immediately if the device is disconnected:

```cpp
bool ok = WaitCondition::WaitDouble(
    []() { return dma.TransferComplete(); },   // target condition
    []() { return device.IsConnected(); },     // mandatory invariant
    2000
);

if (!ok) {
    // Either timed out or device disconnected
}
```

### Using with lambdas that capture state

```cpp
volatile bool rxDone = false;

StartAsyncReceive(&rxDone);

bool ok = WaitCondition::Wait(
    [&]() { return rxDone; },
    5000
);
```
