# WaveGeneratorAdapter

Abstract adapter for the ENS-family arbitrary waveform generator. Generates programmable analog waveforms with configurable positive/negative half-waves, rest/silent periods, electrode switching, and dual-driver support. Used in electrochemical sensing applications.

Header: `#include <Adapter/Port/ENS/Adapter/WaveGeneratorAdapter.h>`

## Template Parameter

```cpp
template<typename HandleType>
class WaveGeneratorAdapter : public IAdapter;
```

`HandleType` — waveform generator register block (e.g., `CMSDK_WAVE_GEN_TypeDef`).

## Option Types

| Type | Base | Description |
|------|------|-------------|
| `ConfigOption` | `IOptionFlag<uint8>` | Configuration flags (rest enable, negative enable, etc.) |
| `CurrentSelectOption` | `IOption<uint8>` | Current source selection (I0–I7) |

## Parameters

```cpp
struct Parameters {
    ConfigOption config;
    uint8 restTime = 0;                   // Rest time (us)
    uint32 silentTime = 0;                // Silent time (us)
    uint32 halfWavePeriod = 0;            // Positive half-wave period (us)
    uint32 negHalfWavePeriod = 0;         // Negative half-wave period (us)
    uint8 clockFreqDiv = 0;              // Clock frequency divider (MHz)
    uint16 alternationLimit = 0;          // Alternation period (clocks)
    uint16 alternationSilentLimit = 0;    // Silent duration per alternation (clocks)
    uint16 delayLimit = 0;               // Initial delay (clocks)
    uint8 negScale = 0;                  // Negative side scale
    uint8 negOffset = 0;                 // Negative side offset
    CurrentSelectOption currentSelect;    // Current source (3 bits)
    uint8 switchConfig = 0;              // Electrode switch bitmask
};
```

## Interrupt Event

```cpp
struct InterruptEvent {
    bool firstAddress;        // First address interrupt triggered
    bool secondAddress;       // Second address interrupt triggered
    uint8 readFirstAddr;      // Current first address value
    uint8 readSecondAddr;     // Current second address value
};
```

## API

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(Parameters val)` | `void` | Apply waveform configuration |
| `Start()` | `void` | Begin waveform generation |
| `Stop()` | `void` | Stop waveform generation |
| `WriteWaveformPoint(uint8 address, uint8 value)` | `void` | Write a point to the waveform lookup table (Driver A) |
| `WriteWaveformPointDriverB(uint8 address, uint8 value, uint8 channel)` | `void` | Write a point to Driver B waveform table |
| `SetInterruptAddresses(uint8 first, uint8 second)` | `void` | Set two address watchpoints for interrupt generation |
| `EnableInterrupt(bool enable)` | `void` | Enable/disable waveform address interrupts |
| `IrqHandler()` | `void` | Call from waveform generator IRQ |

## Callbacks

| Callback | Type | Description |
|----------|------|-------------|
| `onInterrupt` | `std::function<void(InterruptEvent)>` | Address watchpoint triggered |

## BSP Configuration Example

```cpp
// BSP.h
class BSP {
public:
    static AWaveGen waveGen;
};

// BSP.cpp
AWaveGen BSP::waveGen = { WAVE_GEN_DRVA_BLK0 };

// DeviceIrq.cpp
void WG_DRV_Handler() {
    BSP::waveGen.IrqHandler();
}
```

## Usage Example

```cpp
// Fill waveform lookup table with sine wave
for (uint8 i = 0; i < 128; i++) {
    uint8 value = static_cast<uint8>(127.5f + 127.5f * sinf(2.0f * M_PI * i / 128));
    BSP::waveGen.WriteWaveformPoint(i, value);
}

BSP::waveGen.SetParameters({
    .halfWavePeriod = 1000,
    .clockFreqDiv = 8,
    .currentSelect = AWaveGen::CurrentSelect::I3
});

// Set up address interrupt
BSP::waveGen.SetInterruptAddresses(0, 64);
BSP::waveGen.EnableInterrupt(true);

BSP::waveGen.onInterrupt = [](AWaveGen::InterruptEvent event) {
    if (event.firstAddress) {
        // Waveform reached address 0
    }
};

BSP::waveGen.Start();
```
