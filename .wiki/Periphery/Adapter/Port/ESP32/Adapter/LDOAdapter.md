# LDOAdapter

Abstract adapter for ESP32 LDO (Low Dropout) voltage regulator channels. Provides control over on-chip LDO regulators for powering peripherals like MIPI DSI PHY, camera interfaces, etc.

Header: `#include <Adapter/Port/ESP32/Adapter/LDOAdapter.h>`

## Option Types

| Type | Base | Description |
|------|------|-------------|
| `FlagOption` | `IOptionFlag<uint32>` | Channel configuration flags |

## ESP32 Flags (`LDOAdapterESP`)

| Flag | Description |
|------|-------------|
| `Flag::Adjustable` | Voltage can be changed at runtime via `SetVoltage()` |
| `Flag::OwnedByHw` | Channel is owned by hardware (e.g., eFuse), software config may be overridden |

## Parameters

```cpp
struct Parameters {
    uint8 channelId = 0;
    uint32 voltageMv = 0;
    FlagOption flags;
};
```

| Field | Description |
|-------|-------------|
| `channelId` | LDO channel ID per datasheet (e.g., 3 for LDO_VO3) |
| `voltageMv` | Output voltage in millivolts |
| `flags` | Combination of `FlagOption` flags |

## API

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(Parameters val)` | `ResultStatus` | Configure and acquire channel |
| `Acquire()` | `ResultStatus` | Acquire LDO channel |
| `Release()` | `ResultStatus` | Release LDO channel |
| `SetVoltage(uint32 voltageMv)` | `ResultStatus` | Adjust voltage (requires `Adjustable` flag) |

## Usage Example

```cpp
// Fixed voltage LDO for MIPI DSI PHY
BSP::ldo.SetParameters({
    .channelId = 3,
    .voltageMv = 2500,
    .flags = {},
});

// Adjustable LDO
ALDO ldo;
ldo.SetParameters({
    .channelId = 3,
    .voltageMv = 2500,
    .flags = ALDO::Flag::Adjustable,
});
ldo.SetVoltage(3300); // change voltage later
```
