# OS

RTOS abstraction layer for VHAL. Wraps FreeRTOS (or other RTOS backends) into C++ classes with type safety, chrono-based timing, and RAII resource management.

All OS primitives live in the `OS` namespace. Enable them in `VHALConfig.h`:

```cpp
#define VHAL_RTOS
#define VHAL_RTOS_FREERTOS

// Optional primitives (enable as needed):
#define VHAL_RTOS_TIMER
#define VHAL_RTOS_CRITICAL_SECTION
#define VHAL_RTOS_EVENT
#define VHAL_RTOS_MAILBOX
#define VHAL_RTOS_MUTEX
```

## Overview

| Class | Description |
|-------|-------------|
| `RTOS` | Scheduler control: create threads, start scheduler |
| `ThreadStatic<N>` / `Thread<N>` | Base classes for tasks with static or dynamic stack |
| `Mutex` | Mutual exclusion lock |
| `Event` | Event flag group for inter-thread signaling |
| `MailBox<T, N>` | Typed message queue between threads |
| `CriticalSection` | RAII critical section (disables interrupts) |
| `Timer<N>` / `TimerStatic<N>` | Software timer built on a thread with a compile-time stack size |

## FreeRTOS Priorities

The current FreeRTOS wrapper defines `ThreadPriority::clear = 0`, `idle = 1`,
`low = 2`, `belowNormal = 3`, `normal = 4`, `aboveNormal = 5`, `high = 6`, and
`realtime = configMAX_PRIORITIES - 1`. These are the wrapper's numeric values;
`idle` is not the FreeRTOS idle task's priority. Configure `configMAX_PRIORITIES`
so every priority used by your application is in range. Prefer the enum names
over hard-coded values.
