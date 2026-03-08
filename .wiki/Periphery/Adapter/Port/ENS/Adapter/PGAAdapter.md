# PGAAdapter

Abstract adapter for the ENS-family Programmable Gain Amplifier (PGA). Provides configurable input selection, gain control, and output routing for analog signal conditioning in sensor applications.

Header: `#include <Adapter/Port/ENS/Adapter/PGAAdapter.h>`

## Template Parameter

```cpp
template<typename HandleType>
class PGAAdapter : public IAdapter;
```

`HandleType` — analog control register block (e.g., `CMSDK_ANAC_TypeDef`).

## Option Types

| Type | Base | Description |
|------|------|-------------|
| `NegativeInputOption` | `IOption<uint8>` | Inverting input source |
| `PositiveInputOption` | `IOption<uint8>` | Non-inverting input source |
| `GainOption` | `IOption<uint8>` | Amplifier gain setting |
| `OutputOption` | `IOption<uint8>` | Output routing |

## Parameters

```cpp
struct Parameters {
    NegativeInputOption negativeInput;
    PositiveInputOption positiveInput;
    GainOption gain;
    OutputOption output;
};
```

## API

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(Parameters val)` | `void` | Apply PGA configuration |
| `Enable()` | `void` | Enable the amplifier |
| `Disable()` | `void` | Disable the amplifier |

## BSP Configuration Example

```cpp
// BSP.h
class BSP {
public:
    static APGA pga;
};

// BSP.cpp
APGA BSP::pga = { CMSDK_ANAC };
```

## Usage Example

```cpp
BSP::pga.SetParameters({
    .negativeInput = APGA::NegativeInput::Feedback,
    .positiveInput = APGA::PositiveInput::SensorInput,
    .gain = APGA::Gain::X10,
    .output = APGA::Output::ToADC
});

BSP::pga.Enable();
```
