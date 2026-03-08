# DACAdapter

Abstract adapter for DAC (Digital-to-Analog Converter) peripherals. Supports single-shot and continuous (DMA-driven) output with configurable buffer, trigger source, and output connection.

Header: `#include <Adapter/DACAdapter.h>`

## Template Parameters

```cpp
template<typename HandleType, typename DMAHandleType = void*>
class DACAdapter : public IAdapter;
```

## Parameters

```cpp
struct Parameters {
    OutputBuffer outputBuffer = OutputBuffer::Enable;
    OutputConnection outputConnection = OutputConnection::External;
    OutputMode outputMode = OutputMode::Normal;
    TriggerSourceOption triggerSource;
};
```

| Field | Options | Default | Description |
|-------|---------|---------|-------------|
| `outputBuffer` | `Enable`, `Disable` | `Enable` | Output buffer amplifier |
| `outputConnection` | `External`, `Internal` | `External` | Output routing |
| `outputMode` | `Normal`, `SampleHold` | `Normal` | Conversion mode |
| `triggerSource` | `TriggerSourceOption` | — | Trigger for conversion start (platform-specific) |

## Enums

| Enum | Values | Description |
|------|--------|-------------|
| `Error` | `None`, `DmaUnderrun` | Error types |

## API

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(Parameters val)` | `void` | Apply configuration and enable output |
| `Write(uint16 val)` | `ResultStatus` | Set output value |
| `Enable()` | `ResultStatus` | Enable DAC channel |
| `Disable()` | `ResultStatus` | Disable DAC channel |
| `SetDMA(DMAHandleType* dma)` | `void` | Attach DMA adapter |
| `EnableDMARequest()` | `void` | Enable DMA requests |
| `DisableDMARequest()` | `void` | Disable DMA requests |
| `EnableTrigger()` | `void` | Enable conversion trigger |
| `DisableTrigger()` | `void` | Disable conversion trigger |
| `WriteContinuous(uint16* buffer, uint32 count)` | `ResultStatus` | Start continuous DMA output |
| `StopDMA()` | `ResultStatus` | Stop DMA output |
| `IrqHandler()` | `void` | Call from DAC IRQ |

## Callbacks

| Callback | Type | Description |
|----------|------|-------------|
| `onError` | `std::function<void(Error)>` | DMA underrun error |

## BSP Configuration Example

```cpp
// BSP.h
class BSP {
public:
    static ADAC dac1;
};

// BSP.cpp
ADAC BSP::dac1 = { DAC, 1 }; // DAC peripheral, channel 1

void BSP::InitAdapterPeripheryEvents() {
    dac1.beforePeripheryInit = []() {
        LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_DAC1);
        AGPIO::AnalogInit({ GPIOA, 4 }); // DAC output pin

        return ResultStatus::ok;
    };
}
```

## Usage Example

```cpp
BSP::dac1.SetParameters({
    .outputBuffer = ADAC::OutputBuffer::Enable,
    .outputConnection = ADAC::OutputConnection::External
});

// Set output voltage (12-bit: 0..4095)
BSP::dac1.Write(2048); // ~Vref/2

// Continuous waveform via DMA
uint16 sineTable[256] = { /* ... */ };
BSP::dac1.WriteContinuous(sineTable, 256);
```
