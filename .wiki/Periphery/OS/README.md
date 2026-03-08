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
| `Timer` / `TimerStatic` | Software timer built on a thread |
