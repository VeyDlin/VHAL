# TIMAdapter

Abstract adapter for hardware timer peripherals. Supports output compare (PWM), input capture, encoder, and break/dead-time modes with up to 8 channels. Provides flexible clock division, prescaler, and period configuration.

Header: `#include <Adapter/TIMAdapter.h>`

## Template Parameter

```cpp
template<typename HandleType>
class TIMAdapter : public IAdapter;
```

`HandleType` — platform-specific timer register structure.

## Option Types

Platform-specific options defined via the `IOption`/`IOptionDouble` pattern:

| Type | Base | Description |
|------|------|-------------|
| `ClockDivisionOption` | `IOption<uint32>` | Clock division factor |
| `CounterOption` | `IOption<uint32>` | Counter mode (up, down, center-aligned) |
| `OutputCompareOption` | `IOption<uint32>` | Output compare mode (PWM1, PWM2, toggle, etc.) |
| `OutputTriggerOption` | `IOption<uint32>` | Master mode trigger output |
| `OutputTriggerOption2` | `IOption<uint32>` | Second trigger output |
| `ChannelOption` | `IOptionDouble<uint32>` | Channel identifier (primary + complementary) |
| `InterruptOption` | `IOption<uint8>` | Interrupt source selector |
| `InputPrescalerOption` | `IOption<uint32>` | Input capture prescaler |
| `InputFilterOption` | `IOption<uint32>` | Input capture digital filter |
| `InputRemappingOption` | `IOption<uint32>` | Input capture remapping |

## Parameters

```cpp
struct Parameters {
    ClockDivisionOption division;
    uint16 prescaler = 0;
    uint32 period = 1;
    CounterOption mode;
    uint16 repetitionCounter = 0;
    OutputTriggerOption outputTrigger;
    OutputTriggerOption2 outputTrigger2;
};
```

## Output Compare Parameters

```cpp
struct OutputCompareParameters {
    ChannelOption channel;
    OutputCompareOption mode;
    uint32 compare = 0;
    ChannelSelect channelSelect = ChannelSelect::Positive;
    struct { Polarity polarity; State state; IdleState idleState; } positive;
    struct { Polarity polarity; State state; IdleState idleState; } negative;
};
```

## Input Capture Parameters

```cpp
struct InputCaptureParameters {
    ChannelOption channel;
    InputPrescalerOption prescaler;
    InputRemappingOption remapping;
    InputFilterOption filter;
    InputPolarity polarity = InputPolarity::Rising;
    InputSelection selection = InputSelection::Direct;
};
```

## Enums

| Enum | Values | Description |
|------|--------|-------------|
| `State` | `Enable`, `Disable` | Channel output state |
| `Polarity` | `Low`, `High` | Output polarity |
| `IdleState` | `Low`, `High` | Output idle level |
| `Bitness` | `B16`, `B32` | Counter width |
| `InputPolarity` | `Rising`, `Falling`, `RisingFalling` | Input capture edge |
| `InputSelection` | `Direct`, `Indirect`, `TRC` | Input capture source |
| `ChannelSelect` | `Positive`, `Negative`, `PositiveNegative`, `NoOutput` | Channel output selection |
| `ChannelEnableSelect` | `EnablePositive`, `EnableNegative`, `EnableAll`, `DisablePositive`, `DisableNegative`, `DisableAll` | Enable/disable channels |

## Configuration API

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(Parameters val)` | `ResultStatus` | Apply base timer configuration |
| `ConfigOutputCompareParameters(initializer_list)` | `ResultStatus` | Configure output compare channels |
| `ConfigInputCaptureParameters(initializer_list)` | `ResultStatus` | Configure input capture channels |
| `ConfigBreakAndDeadTimeParameters(initializer_list)` | `ResultStatus` | Configure break/dead-time |
| `EnableInterrupt(initializer_list<InterruptOption>)` | `ResultStatus` | Enable interrupts |
| `DisableInterrupt(initializer_list<InterruptOption>)` | `ResultStatus` | Disable interrupts |

## Runtime API

| Method | Return | Description |
|--------|--------|-------------|
| `EnableCounter(bool)` | `void` | Start/stop the timer counter |
| `SetChannelsState(initializer_list<EnableChannelParameters>)` | `void` | Enable/disable channel outputs |
| `SetDivision(ClockDivisionOption)` | `void` | Change clock division |
| `SetPrescaler(uint32)` | `void` | Change prescaler |
| `SetPeriod(uint32)` | `void` | Change auto-reload period |
| `SetCompare(ChannelOption, uint32)` | `void` | Change compare value (duty cycle) |
| `SetCompareMode(ChannelOption, OutputCompareOption)` | `void` | Change output compare mode |
| `GetCapture(ChannelOption)` | `uint32` | Read input capture value |
| `GenerateUpdateEvent()` | `void` | Force update event |
| `GetClockDivision()` | `uint16` | Get current clock division |
| `GetBusClockHz()` | `uint32` | Get input bus clock frequency |
| `GetBitness()` | `Bitness` | Get counter width (16/32 bit) |
| `GetChannelIndex(ChannelOption)` | `uint8` | Get array index for channel |
| `IrqHandler()` | `void` | Call from timer IRQ |

## Callbacks

| Callback | Type | Description |
|----------|------|-------------|
| `onInputCaptureEvent` | `std::function<void(uint8 channel)>` | Input capture triggered |
| `onOutputCompareEvent` | `std::function<void(uint8 channel)>` | Output compare match |
| `onUpdateEvent` | `std::function<void()>` | Timer overflow/underflow |
| `onBreakInputEvent` | `std::function<void()>` | Break input detected |
| `onBreak2InputEvent` | `std::function<void()>` | Break 2 input detected |
| `onTriggerDetectionEvent` | `std::function<void()>` | Trigger signal detected |
| `onCommutationEvent` | `std::function<void()>` | Commutation event |

## BSP Configuration Example

```cpp
// BSP.h
class BSP {
public:
    static ATIM timer1;
};

// BSP.cpp
ATIM BSP::timer1 = { TIM1, SystemCoreClock, ATIM::Bitness::B16 };

void BSP::InitAdapterPeripheryEvents() {
    timer1.beforePeripheryInit = []() {
        LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_TIM1);

        // PWM output pin
        AGPIO::AlternateInit({ GPIOA, 8, 2, AGPIO::Pull::None, AGPIO::Speed::VeryHigh });

        NVIC_SetPriority(TIM1_CC_IRQn, 2);
        NVIC_EnableIRQ(TIM1_CC_IRQn);

        return ResultStatus::ok;
    };
}

// DeviceIrq.cpp
void TIM1_CC_IRQHandler() {
    BSP::timer1.IrqHandler();
}
```

## Usage Example (PWM)

```cpp
BSP::timer1.SetParameters({
    .prescaler = 0,
    .period = 1000,
    .mode = ATIM::Counter::Up
});

BSP::timer1.ConfigOutputCompareParameters({
    {
        .channel = ATIM::Channel::Ch1,
        .mode = ATIM::OutputCompare::PWM1,
        .compare = 500, // 50% duty cycle
        .channelSelect = ATIM::ChannelSelect::Positive,
        .positive = { .polarity = ATIM::Polarity::High, .state = ATIM::State::Enable }
    }
});

BSP::timer1.SetChannelsState({
    { ATIM::Channel::Ch1, ATIM::ChannelEnableSelect::EnablePositive }
});

BSP::timer1.EnableCounter(true);

// Change duty at runtime
BSP::timer1.SetCompare(ATIM::Channel::Ch1, 750); // 75%
```
