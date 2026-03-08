# PMUAdapter

Abstract adapter for the ENS-family Power Management Unit (PMU). Provides Low Voltage Detection (LVD), temperature protection, and bandgap reference management.

Header: `#include <Adapter/Port/ENS/Adapter/PMUAdapter.h>`

## Template Parameter

```cpp
template<typename HandleType>
class PMUAdapter : public IAdapter;
```

`HandleType` — analog control register block (e.g., `CMSDK_ANAC_TypeDef`).

## Option Types

| Type | Base | Description |
|------|------|-------------|
| `LvdThresholdOption` | `IOption<uint8>` | Low voltage detection threshold |

## API

### Low Voltage Detection (LVD)

| Method | Return | Description |
|--------|--------|-------------|
| `SetLvdThreshold(LvdThresholdOption)` | `void` | Set LVD trigger voltage |
| `EnableLvd(bool enable)` | `void` | Enable/disable LVD |
| `GetLvdStatus()` | `bool` | Check if voltage is below threshold |

### Temperature Protection

| Method | Return | Description |
|--------|--------|-------------|
| `EnableTemperatureProtection(bool enable)` | `void` | Enable/disable over-temperature protection |
| `GetTemperatureAlert()` | `bool` | Check if temperature alert is active |

### Bandgap Reference

| Method | Return | Description |
|--------|--------|-------------|
| `EnableBandgap(bool enable)` | `void` | Enable/disable internal bandgap reference |

## BSP Configuration Example

```cpp
// BSP.h
class BSP {
public:
    static APMU pmu;
};

// BSP.cpp
APMU BSP::pmu = { CMSDK_ANAC };
```

## Usage Example

```cpp
// Enable bandgap reference (required before ADC/comparator use)
BSP::pmu.EnableBandgap(true);

// Configure low voltage detection
BSP::pmu.SetLvdThreshold(APMU::LvdThreshold::V2_7);
BSP::pmu.EnableLvd(true);

// Check power status
if (BSP::pmu.GetLvdStatus()) {
    // Low voltage detected — enter safe mode
}

// Enable temperature protection
BSP::pmu.EnableTemperatureProtection(true);
```
