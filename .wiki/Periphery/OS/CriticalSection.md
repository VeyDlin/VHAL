# CriticalSection

RAII wrapper that disables interrupts on construction and re-enables them on destruction. Protects against both thread preemption and ISR interference.

## Usage

```cpp
{
    OS::CriticalSection cs;
    // Interrupts are disabled here
    // No thread switching, no ISRs
    sharedCounter++;
} // Interrupts re-enabled automatically when `cs` goes out of scope
```

The RAII pattern guarantees interrupts are re-enabled even if an exception occurs or if you return early from the block.

## When to Use

| Scenario | Recommended primitive |
|----------|----------------------|
| A few register writes that must be atomic (< 10 instructions) | `CriticalSection` |
| Protecting a shared buffer accessed by multiple threads | `Mutex` |
| Waking a thread from an ISR | Thread `Signal()` or `Event` |
| Passing data from ISR to thread | `MailBox` |

`CriticalSection` disables **all** interrupts, so it blocks everything — RTOS tick, UART receive, timers. Keep the critical section as short as possible (microseconds, not milliseconds).

## Example: Atomic Read-Modify-Write

```cpp
volatile uint32_t sharedFlags = 0;

// In thread:
void SetFlag(uint32_t flag) {
    OS::CriticalSection cs;
    sharedFlags |= flag;
}

// In ISR:
void ClearFlag(uint32_t flag) {
    OS::CriticalSection cs;
    sharedFlags &= ~flag;
}
```

Without the critical section, a read-modify-write on `sharedFlags` could be interrupted between the read and write, losing an update.

## Nesting

Critical sections can be nested safely — interrupts are only re-enabled when the outermost `CriticalSection` is destroyed:

```cpp
{
    OS::CriticalSection outer;
    // interrupts disabled
    {
        OS::CriticalSection inner;
        // still disabled
    }
    // still disabled (outer is still alive)
}
// interrupts re-enabled
```

## Enable

Add to `VHALConfig.h`:

```cpp
#define VHAL_RTOS_CRITICAL_SECTION
```
