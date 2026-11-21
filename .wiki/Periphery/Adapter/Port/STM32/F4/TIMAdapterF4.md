# TIMAdapterF4

The [TIM adapter](../../../TIMAdapter.md) on the STM32F4 timers: general purpose TIM2 to TIM5, TIM9 to TIM14, and the advanced timers TIM1 and TIM8 where the chip has them.

Header: `#include <Adapter/Port/STM32/F4/TIMAdapterF4.h>`

Class: `TIMAdapterF4`. Alias: `ATIM`. The constructor takes the timer handle, the bus clock in hertz, and the bit width (`Bitness::B16` by default).

The BSP does the following before the first `Initialization()`:

- Enables the bus clock of each timer it uses. The APB2 timers are TIM1, TIM8, TIM9, TIM10 and TIM11. The APB1 timers are TIM2 to TIM7, TIM12, TIM13 and TIM14.
- Routes the timer vectors to `IrqHandler()` of the adapter that owns each timer. Some vectors serve two timers (for example `TIM1_BRK_TIM9`), so the vector calls `IrqHandler()` of both adapters. Each adapter reads only the flags of its own timer.

The adapter does not touch the NVIC.

## What works

| Method | Notes |
|--------|-------|
| `SetPrescaler()`, `SetPeriod()`, `SetDivision()` | Division is `D1`, `D2` or `D4`. |
| `EnableCounter()`, `GenerateUpdateEvent()` | |
| `SetChannelsState()` | Enables and disables the channels and their complementary outputs. |
| `SetCompare()`, `SetCompareMode()`, `GetCapture()` | Output compare on channels 1 to 4. `GetCapture()` reads the capture register. |
| `OutputCompareInitialization()` | Preload on, fast mode off. Outputs enabled on TIM1 and TIM8. |
| `SetInterrupt()` | Capture/compare 1 to 4, update, break, trigger and commutation. |
| `IrqHandler()` | Capture/compare events on channels 1 to 4, update, break, trigger, commutation. |

Interrupts on channels 2 and 4 work. The channel 2 event reads the CC2 flag and CC2 enable, and the channel 4 event reads the CC4 flag and enable. Each channel reads its input or output select from its own CCMR register: CCMR1 for channels 1 and 2, CCMR2 for channels 3 and 4.

## Input capture

`ConfigInputCaptureParameters()` sets each channel of the list as an input:

| Field | Written to | Values |
|-------|------------|--------|
| `selection` | CCMR1 (channels 1 and 2) or CCMR2 (channels 3 and 4) | `Direct`, `Indirect` or `TRC` |
| `prescaler` | the same register, the prescaler bits of the channel | `InputPrescaler::Div1`, `Div2`, `Div4` or `Div8` |
| `filter` | the same register, the filter bits of the channel | a raw LL filter value, such as `LL_TIM_IC_FILTER_FDIV1` |
| `polarity` | CCER, the CCxP and CCxNP bits of the channel | `Rising`, `Falling` or `RisingFalling` (both edges) |
| `remapping` | OR | a raw LL remap value. Zero writes nothing, so the default leaves OR as it is |

```cpp
ATIM::InputCaptureParameters capture;
capture.channel = ATIM::Channel::C2;
capture.prescaler = ATIM::InputPrescaler::Div1;
capture.filter = ATIM::InputFilterOption(LL_TIM_IC_FILTER_FDIV1);
capture.remapping = ATIM::InputRemappingOption(0);
capture.polarity = ATIM::InputPolarity::RisingFalling;
capture.selection = ATIM::InputSelection::Direct;

tim.ConfigInputCaptureParameters({ capture });
tim.SetChannelsState({ { ATIM::Channel::C2, ATIM::ChannelEnableSelect::EnablePositive } });
tim.EnableInterrupt({ ATIM::Interrupt::CaptureCompare2 });
```

The capture value is read with `GetCapture()`. A capture flag of a channel whose select is not zero calls `onInputCaptureEvent(channel)`. A channel whose select is zero (an output) calls `onOutputCompareEvent(channel)`.

`RisingFalling` sets CCxP and CCxNP as the vendor LL header defines them. Whether the general-purpose timers use CCxNP for both-edge capture is not checked against RM0090 yet.

The adapter does not disable the channel before it writes the select, so stop a channel that runs with `SetChannelsState()` before you change its input settings.

## Not supported

- Break and dead-time configuration (`BreakAndDeadTimeInitialization()` returns `notSupported`).
- Interrupt options outside the table return `notSupported`. The F4 keeps this answer so that a request the hardware cannot honour is reported. G0 and G4 return `ok` for the same options. That difference is a G0/G4 change, not an F4 one.
