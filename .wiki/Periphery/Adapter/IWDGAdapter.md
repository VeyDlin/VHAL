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

## ESP32 Port

`AIWDG` is `IWDGAdapterESP` (`Periphery/Adapter/Port/ESP32/Common/IWDGAdapterESP.h`). It is not a hardware IWDG: it uses the ESP-IDF task watchdog (TWDT). `HandleType` is `void`, so the BSP declares it as `static AIWDG mcuWatchDog;` and passes no peripheral.

- `SetParameters()` sets the TWDT timeout to `minDeadlineMs` and watches the idle task of every core, so a task that starves the CPU also trips it. A missed deadline panics, which resets the chip.
- `GetDeadlineMs()` returns `minDeadlineMs` unchanged. The TWDT has no prescaler, so there is no rounding.
- If ESP-IDF already runs the TWDT (`CONFIG_ESP_TASK_WDT_INIT`), `SetParameters()` reconfigures it. The timeout is global: it also applies to every other task subscribed to the TWDT.
- `Start()` subscribes the calling task and feeds the TWDT once. Call `Start()` from the task that will call `Reset()`. `Reset()` feeds the TWDT for the calling task only, so a call from another task does not keep this one alive.
- `Start()` cannot be undone. An ESP-IDF error in `Start()` or `Reset()` aborts through `ESP_ERROR_CHECK`.
- The demo's watchdog task is off by default (`Application::useWatchDog`), see `.demo/ESP32/README.md`.

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
