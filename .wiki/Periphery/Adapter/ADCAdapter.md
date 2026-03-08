# ADCAdapter

Abstract adapter for ADC (Analog-to-Digital Converter) peripherals. Supports multi-channel scanning, regular and injected conversion groups, configurable resolution, DMA integration, and interrupt-driven conversion.

Header: `#include <Adapter/ADCAdapter.h>`

## Template Parameters

```cpp
template<typename HandleType, typename DMAHandleType = void*>
class ADCAdapter : public IAdapter;
```

- `HandleType` — platform-specific ADC register structure
- `DMAHandleType` — DMA handle type (optional, defaults to `void*`)

## Option Types

| Type | Base | Description |
|------|------|-------------|
| `TriggerSourceOption` | `IOption<uint32>` | Conversion trigger source (platform-specific values) |
| `CommonSamplingOption` | `IOption<uint32>` | Common sampling time (platform-specific values) |

## Parameters

```cpp
struct Parameters {
    ScanMode scanMode = ScanMode::Enable;
    Resolution resolution = Resolution::B12;
    DataAlignment dataAlignment = DataAlignment::Right;
};
```

## Regular & Injected Groups

```cpp
struct RegularParameters {
    TriggerSourceOption triggerSource;
    ContinuousMode continuousMode = ContinuousMode::Single;
};

struct RegularChannel {
    uint8 channel;
    uint32 maxSamplingTimeNs;
};

struct InjectedParameters {
    TriggerSourceOption triggerSource;
    ContinuousMode continuousMode = ContinuousMode::Single;
};

struct InjecteChannel {
    uint8 channel;
    uint32 maxSamplingTimeNs;
};
```

## Enums

| Enum | Values | Description |
|------|--------|-------------|
| `Resolution` | `B6`, `B8`, `B10`, `B12`, `B14`, `B16` | ADC resolution in bits |
| `Irq` | `Conversion`, `Injected`, `Watchdog`, `Sampling`, `ConfigReady` | Interrupt sources |
| `Error` | `None`, `Overrun`, `ModeFault`, `FrameFormat`, `AdcConfig` | Error types |

## API

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(Parameters val)` | `ResultStatus` | Apply base configuration |
| `ConfigRegularGroup(RegularParameters, initializer_list<RegularChannel>)` | `ResultStatus` | Configure regular conversion group |
| `ConfigInjectedGroup(initializer_list<InjecteChannel>)` | `ResultStatus` | Configure injected conversion group |
| `Read<DataType>()` | `Result<DataType>` | Synchronous single conversion |
| `ReadArray<DataType>(DataType* buffer, uint32 size)` | `ResultStatus` | Synchronous multi-channel read |
| `ReadAsync<DataType>(DataType& data)` | `ResultStatus` | Start async conversion |
| `ReadArrayAsync<DataType>(DataType* buffer, uint32 size)` | `ResultStatus` | Start async multi-channel read |
| `Calibration()` | `ResultStatus` | Run ADC self-calibration |
| `SetTimeout(uint32 val)` | `void` | Set conversion timeout (ms) |
| `SetDMA(DMAHandleType* dma)` | `void` | Attach DMA adapter for transfers |
| `IrqHandler()` | `void` | Call from ADC IRQ |
| `AbortRegular()` | `void` | Abort regular group conversion |
| `AbortInjected()` | `void` | Abort injected group |
| `AbortAll()` | `void` | Abort all conversions |

## Callbacks

| Callback | Type | Description |
|----------|------|-------------|
| `onInterrupt` | `std::function<void(Irq, uint8 channel)>` | Conversion complete (with channel number) |
| `onError` | `std::function<void(Error)>` | Conversion error |

## BSP Configuration Example

```cpp
// BSP.h
class BSP {
public:
    static AADC adc1;
};

// BSP.cpp
AADC BSP::adc1 = { ADC1 };

void BSP::InitAdapterPeripheryEvents() {
    adc1.beforePeripheryInit = []() {
        LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_ADC);

        // Configure analog input pins
        AGPIO::AnalogInit({ GPIOA, 0 }); // Channel 0
        AGPIO::AnalogInit({ GPIOA, 1 }); // Channel 1

        NVIC_SetPriority(ADC1_IRQn, 2);
        NVIC_EnableIRQ(ADC1_IRQn);

        return ResultStatus::ok;
    };
}

// DeviceIrq.cpp
void ADC1_IRQHandler() {
    BSP::adc1.IrqHandler();
}
```

## Usage Example

```cpp
BSP::adc1.SetParameters({
    .scanMode = AADC::ScanMode::Enable,
    .resolution = AADC::Resolution::B12,
    .dataAlignment = AADC::DataAlignment::Right
});

BSP::adc1.ConfigRegularGroup(
    { .continuousMode = AADC::ContinuousMode::Single },
    { { 0, 5000 }, { 1, 5000 } } // channel, maxSamplingTimeNs
);

BSP::adc1.Calibration();

// Single conversion
auto result = BSP::adc1.Read<uint16>();
if (result.IsOk()) {
    uint16 adcValue = result.Value();
}
```
