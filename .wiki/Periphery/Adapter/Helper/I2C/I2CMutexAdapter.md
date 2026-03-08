# I2CMutexAdapter

Template wrapper that adds mutex protection around every I2C operation. Wraps any I2C adapter class, calling a user-provided `onMutex` callback before and after each bus transaction.

Header: `#include <Adapter/Helper/I2C/I2CMutexAdapter.h>`

## Usage

```cpp
I2CMutexAdapter<AI2C> sharedI2C(&hi2c1, 100000);

sharedI2C.onMutex = [](bool lock) {
    if (lock) {
        i2cMutex.Lock();
    } else {
        i2cMutex.UnLock();
    }
};
```

Now every `CheckDevice`, `WriteByteArray`, `ReadByteArray`, `Scan`, and their async variants will automatically lock/unlock the mutex.

## Template Parameter

```cpp
template<class AdapterClass>
class I2CMutexAdapter : public AdapterClass;
```

`AdapterClass` — any class that inherits from `I2CAdapter` (e.g. the platform-specific `AI2C`). `I2CMutexAdapter` inherits from it and overrides all virtual methods to wrap them with mutex calls.

## Constructors

```cpp
I2CMutexAdapter();
I2CMutexAdapter(HandleT *i2c, uint32 busClockHz);
```

Same parameters as the underlying `AdapterClass` constructor.

## Callback

| Field | Type | Description |
|-------|------|-------------|
| `onMutex` | `std::function<void(bool lock)>` | Called with `true` before an operation, `false` after. If not set, operations proceed without locking |

## Wrapped Methods

All methods follow the same pattern: lock → call base → unlock → return result.

| Method | Description |
|--------|-------------|
| `CheckDevice` | Synchronous device check |
| `CheckDeviceAsync` | Async device check (locks, starts, awaits, unlocks) |
| `Scan` | Synchronous bus scan |
| `ScanAsync` | Async bus scan |
| `WriteByteArray` | Synchronous write |
| `ReadByteArray` | Synchronous read |
| `WriteByteArrayAsync` | Async write (locks, starts, awaits, unlocks) |
| `ReadByteArrayAsync` | Async read (locks, starts, awaits, unlocks) |

Async variants call the base async method, then `Await()`, then unlock — so the mutex is held for the entire transfer duration.

## Use Case

When multiple RTOS threads share a single I2C bus, wrap the adapter with `I2CMutexAdapter` and provide an `OS::Mutex`-based callback. Each thread can then use the adapter without manual locking.

```cpp
// BSP
I2CMutexAdapter<AI2C> i2c1;

// Application init
OS::Mutex i2cMutex;

i2c1.onMutex = [&](bool lock) {
    lock ? i2cMutex.Lock() : i2cMutex.UnLock();
};

// Thread A                          // Thread B
i2c1.WriteByteArray(...);           i2c1.ReadByteArray(...);
// Both safely share the same bus
```
