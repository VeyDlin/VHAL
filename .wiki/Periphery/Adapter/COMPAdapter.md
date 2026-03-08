# COMPAdapter

Abstract adapter for analog comparator peripherals. Compares two analog inputs and produces a digital output. Supports configurable hysteresis, output blanking, polarity inversion, and interrupt/event trigger modes.

Header: `#include <Adapter/COMPAdapter.h>`

## Template Parameter

```cpp
template<typename HandleType>
class COMPAdapter : public IAdapter;
```

## Option Types

| Type | Base | Description |
|------|------|-------------|
| `InputPlusOption` | `IOption<uint32>` | Non-inverting input source |
| `InputMinusOption` | `IOption<uint32>` | Inverting input source |
| `InputHysteresisOption` | `IOption<uint32>` | Hysteresis level |
| `OutputBlankingSourceOption` | `IOption<uint32>` | Output blanking timer source |

## Parameters

```cpp
struct Parameters {
    InputPlusOption inputPlus;
    InputMinusOption inputMinus;
    InputHysteresisOption inputHysteresis;
    OutputBlankingSourceOption outputBlankingSource;
    OutputPolarity outputPolarity = OutputPolarity::NotInverted;
    TriggerMode triggerMode = TriggerMode::None;
};
```

## Enums

| Enum | Values | Description |
|------|--------|-------------|
| `OutputPolarity` | `NotInverted`, `Inverted` | Comparator output polarity |
| `TriggerMode` | `None`, `InterruptRising`, `InterruptFalling`, `InterruptRisingFalling`, `EventRising`, `EventFalling`, `EventRisingFalling` | Trigger on output transitions |

## API

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(Parameters val)` | `void` | Apply configuration and enable comparator |
| `GetOutputLevel()` | `bool` | Read current output level |
| `Enable()` | `ResultStatus` | Enable comparator |
| `Disable()` | `ResultStatus` | Disable comparator |
| `SetInputPlus(InputPlusOption)` | `ResultStatus` | Change non-inverting input |
| `SetInputMinus(InputMinusOption)` | `ResultStatus` | Change inverting input |
| `SetInputHysteresis(InputHysteresisOption)` | `ResultStatus` | Change hysteresis |
| `SetOutputPolarity(OutputPolarity)` | `ResultStatus` | Change output polarity |
| `SetOutputBlankingSource(OutputBlankingSourceOption)` | `ResultStatus` | Change blanking source |
| `SetTriggerMode(TriggerMode)` | `ResultStatus` | Change trigger mode |
| `IrqHandler()` | `void` | Call from comparator IRQ |

## Callbacks

| Callback | Type | Description |
|----------|------|-------------|
| `onInterrupt` | `std::function<void(bool outputLevel)>` | Called on trigger event with current output level |

## BSP Configuration Example

```cpp
// BSP.h
class BSP {
public:
    static ACOMP comp1;
};

// BSP.cpp
ACOMP BSP::comp1 = { COMP1 };

void BSP::InitAdapterPeripheryEvents() {
    comp1.beforePeripheryInit = []() {
        // Configure analog input pins
        AGPIO::AnalogInit({ GPIOA, 1 }); // COMP1_INP
        AGPIO::AnalogInit({ GPIOA, 0 }); // COMP1_INM

        NVIC_SetPriority(COMP_IRQn, 2);
        NVIC_EnableIRQ(COMP_IRQn);

        return ResultStatus::ok;
    };
}

// DeviceIrq.cpp
void COMP_IRQHandler() {
    BSP::comp1.IrqHandler();
}
```

## Usage Example

```cpp
BSP::comp1.SetParameters({
    .inputPlus = ACOMP::InputPlus::PA1,
    .inputMinus = ACOMP::InputMinus::InternalRef,
    .inputHysteresis = ACOMP::Hysteresis::Medium,
    .triggerMode = ACOMP::TriggerMode::InterruptRisingFalling
});

BSP::comp1.onInterrupt = [](bool level) {
    if (level) {
        // Input crossed above reference
    }
};
```
