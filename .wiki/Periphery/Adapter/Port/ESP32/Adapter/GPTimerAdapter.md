# GPTimerAdapter

Abstract adapter for ESP32 General Purpose Timer. Provides a hardware timer with configurable resolution, alarm events, and auto-reload for periodic callbacks. Unlike the generic `TIMAdapter`, this is a simplified interface specific to the ESP-IDF GPTimer peripheral.

Header: `#include <Adapter/Port/ESP32/Adapter/GPTimerAdapter.h>`

> **Note:** This adapter does not use a template parameter — ESP-IDF manages timer handles internally.

## Parameters

```cpp
struct Parameters {
    uint32 resolutionHz = 1000000;
    uint64 alarmCount = 0;
    bool autoReload = true;
    CountDirection direction = CountDirection::Up;
};
```

| Field | Options | Default | Description |
|-------|---------|---------|-------------|
| `resolutionHz` | `uint32` | `1000000` | Timer tick frequency (1 MHz = 1us resolution) |
| `alarmCount` | `uint64` | `0` | Counter value that triggers alarm |
| `autoReload` | `bool` | `true` | Auto-reset counter on alarm |
| `direction` | `Up`, `Down` | `Up` | Count direction |

## API

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(Parameters val)` | `ResultStatus` | Configure timer |
| `Start()` | `ResultStatus` | Start counting |
| `Stop()` | `ResultStatus` | Stop counting |
| `SetAlarm(uint64 alarm, bool reload)` | `ResultStatus` | Change alarm value and reload behavior |
| `SetCount(uint64 count)` | `ResultStatus` | Set current counter value |
| `GetCount()` | `uint64` | Read current counter value |

## Callbacks

| Callback | Type | Description |
|----------|------|-------------|
| `onAlarm` | `std::function<void()>` | Called when counter reaches alarm value |

## Usage Example

```cpp
GPTimerAdapter timer;

timer.SetParameters({
    .resolutionHz = 1000000,  // 1 MHz → 1us per tick
    .alarmCount = 1000000,    // Alarm every 1 second
    .autoReload = true,
    .direction = GPTimerAdapter::CountDirection::Up
});

timer.onAlarm = []() {
    // Called every 1 second
};

timer.Start();
```
