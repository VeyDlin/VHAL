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

On STM32 G0, attach the DMA adapter with `SetDMA()` before starting a DMA-backed read. `SetDMA()` installs the DMA completion and error callbacks used by the ADC adapter. DMA behavior is port-specific; this wiring is not provided by every ADC port.

## Callbacks

| Callback | Type | Description |
|----------|------|-------------|
| `onInterrupt` | `std::function<void(Irq, uint8 channel)>` | Conversion complete (with channel number) |
| `onError` | `std::function<void(Error)>` | Conversion error |

## ESP32 Port

`AADC` is `ADCAdapterESP` (`Periphery/Adapter/Port/ESP32/Common/ADCAdapterESP.h`), built on the ESP-IDF `esp_adc` oneshot and calibration API. `HandleType` is `void`.

```cpp
AADC(adc_unit_t unit);
```

The project must list the `esp_adc` ESP-IDF component in `REQUIRES` of its `main/CMakeLists.txt`.

- Resolution: `SetParameters()` returns `ResultStatus::notSupported` when the resolution is outside `SOC_ADC_RTC_MIN_BITWIDTH` to `SOC_ADC_RTC_MAX_BITWIDTH`. On classic ESP32 this range is 9 to 12 bits. Samples wider than 8 bits are read as 16-bit values.
- Regular groups hold up to 8 channels. `ConfigInjectedGroup()` returns `notSupported`. `maxSamplingTimeNs` is not used on ESP32.
- `SetAttenuation(AttenuationOption val)` takes one of `AADC::Attenuation::Db0`, `Db2_5`, `Db6`, `Db12` (default `Db12`). The value applies to channels configured after the call, so call it before `ConfigRegularGroup()`. Channels already configured keep their attenuation.
- `Calibration()` creates calibration data for every configured channel of the regular group. Call it after `ConfigRegularGroup()`. It returns `noInit` before `SetParameters()`, `notReady` when no channel is configured, `notSupported` when the ESP-IDF build has no calibration scheme, and `error` on driver failure. Reconfiguring a rank discards its calibration, so run `Calibration()` again.
- `ReadVoltage(uint8 rank = 1)` converts the channel of the given rank and returns `Result<uint32>` in millivolts. Errors: `noInit` before `SetParameters()`, `outOfRange` when the rank is not configured, `notReady` when that rank has no calibration, `error` on driver failure.
- `RawToVoltage(uint32 raw, uint8 rank = 1)` converts a raw sample for the given rank without starting a conversion and returns `Result<uint32>` in millivolts. Errors: `outOfRange`, `notReady`, `error`.
- `ReadByteArrayAsync` (`ReadAsync`, `ReadArrayAsync`) returns `notSupported`.

Reading a voltage in millivolts:

```cpp
AADC adc1(ADC_UNIT_1);

adc1.SetParameters({
    .resolution = AADC::Resolution::B12
});

adc1.SetAttenuation(AADC::Attenuation::Db12);
adc1.ConfigRegularGroup({ .continuousMode = AADC::ContinuousMode::Single }, { { 0, 0 } }); // ADC1 channel 0
adc1.Calibration();

Result<uint32> voltage = adc1.ReadVoltage(1);
if (voltage.IsOk()) {
    uint32 millivolts = voltage.Value();
}
```

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

// After configuring BSP::dma1ch1 for the ADC transfer, attach it on STM32 G0.
BSP::adc1.SetDMA(&BSP::dma1ch1);

// Single conversion
auto result = BSP::adc1.Read<uint16>();
if (result.IsOk()) {
    uint16 adcValue = result.Value();
}
```
