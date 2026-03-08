# BoostAdapter

Abstract adapter for the ENS-family boost converter peripheral. Controls a voltage boost stage used to power electrochemical sensors with selectable output voltage levels and external mode support.

Header: `#include <Adapter/Port/ENS/Adapter/BoostAdapter.h>`

## Template Parameter

```cpp
template<typename HandleType>
class BoostAdapter : public IAdapter;
```

`HandleType` — analog control register block (e.g., `CMSDK_ANAC_TypeDef`).

## Option Types

| Type | Base | Description |
|------|------|-------------|
| `VoltageSelectOption` | `IOption<uint8>` | Output voltage level selection |

## Parameters

```cpp
struct Parameters {
    VoltageSelectOption voltage;
    bool externalMode = false;
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `voltage` | — | Output voltage level (platform defines available values, e.g., V11, V15, V26, V45, V55) |
| `externalMode` | `false` | Use external boost circuit instead of internal |

## API

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(Parameters val)` | `ResultStatus` | Configure boost voltage |
| `Enable()` | `void` | Enable boost converter output |
| `Disable()` | `void` | Disable boost converter output |
| `Standby(bool enable)` | `void` | Enter/exit standby mode |
| `IsEnabled()` | `bool` | Check if boost is active |

## BSP Configuration Example

```cpp
// BSP.h
class BSP {
public:
    static ABoost boost;
};

// BSP.cpp
ABoost BSP::boost = { CMSDK_ANAC };
```

## Usage Example

```cpp
BSP::boost.SetParameters({
    .voltage = ABoost::VoltageSelect::V26,
    .externalMode = false
});

BSP::boost.Enable();

// Later...
if (BSP::boost.IsEnabled()) {
    BSP::boost.Standby(true); // Enter low-power mode
}
```
