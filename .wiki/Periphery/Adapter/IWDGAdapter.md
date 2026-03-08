# IWDGAdapter

Abstract adapter for Independent Watchdog (IWDG) peripherals. Provides a hardware watchdog timer that resets the MCU if not periodically refreshed, protecting against software lockups.

Header: `#include <Adapter/IWDGAdapter.h>`

## Template Parameter

```cpp
template<typename HandleType>
class IWDGAdapter : public IAdapter;
```

## Parameters

```cpp
struct Parameters {
    uint32 minDeadlineMs = 1000;
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `minDeadlineMs` | `1000` | Minimum watchdog timeout in milliseconds. The actual deadline may be slightly longer depending on available prescaler/reload combinations |

## API

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(Parameters val)` | `ResultStatus` | Configure watchdog timeout |
| `Start()` | `void` | Start the watchdog (cannot be stopped once started) |
| `Reset()` | `void` | Refresh the watchdog counter (must be called before deadline) |
| `GetDeadlineMs()` | `uint32` | Get the actual configured deadline (ms) |

## BSP Configuration Example

```cpp
// BSP.h
class BSP {
public:
    static AIWDG mcuWatchDog;
};

// BSP.cpp
AIWDG BSP::mcuWatchDog = { IWDG, 32000 }; // IWDG peripheral, LSI clock = 32kHz
```

## Usage Example

```cpp
BSP::mcuWatchDog.SetParameters({
    .minDeadlineMs = 2000 // 2 second timeout
});
BSP::mcuWatchDog.Start();

// Main loop must call Reset() periodically
while (true) {
    // Application logic
    BSP::mcuWatchDog.Reset();
}
```

> **Warning:** Once started, the IWDG cannot be stopped. A reset occurs if `Reset()` is not called within the deadline period.
