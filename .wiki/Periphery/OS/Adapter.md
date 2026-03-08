# OS Adapter

Aggregate include header for the OS module. Automatically includes all enabled OS primitives based on `VHALConfig.h` defines.

## Always included

- `RTOS` — scheduler control, thread creation
- `Thread` — base classes for tasks (`ThreadStatic`, `Thread`)

## Conditionally included

| Define | Includes |
|--------|----------|
| `VHAL_RTOS_TIMER` | `Timer` — software timers built on threads |
| `VHAL_RTOS_CRITICAL_SECTION` | `CriticalSection` — RAII interrupt disable/enable |
| `VHAL_RTOS_EVENT` | `Event` — event flag groups for inter-thread signaling |
| `VHAL_RTOS_MAILBOX` | `MailBox` — typed message queues |
| `VHAL_RTOS_MUTEX` | `Mutex` — mutual exclusion locks |

## Usage

You don't typically include this file directly. It's included by `VHAL.h` when `VHAL_RTOS` is defined in your `VHALConfig.h`. To enable specific primitives, add the corresponding defines:

```cpp
// VHALConfig.h
#define VHAL_RTOS
#define VHAL_RTOS_FREERTOS

#define VHAL_RTOS_MUTEX
#define VHAL_RTOS_EVENT
#define VHAL_RTOS_MAILBOX
#define VHAL_RTOS_TIMER
#define VHAL_RTOS_CRITICAL_SECTION
```
