# ITIMHelper

Base helper class for working with a single timer channel. Provides convenience methods for channel control, frequency calculation, and prescaler/period/compare manipulation.

Header: `#include <Adapter/Helper/TIM/ITIMHelper.h>`

## TimerChannel

```cpp
struct TimerChannel {
    ATIM *timer;
    ATIM::ChannelOption channel;
};
```

A lightweight struct that pairs a timer adapter with a channel. Useful for passing timer+channel as a single parameter.

## Template Parameter

```cpp
template <RealType Type = float>
class ITIMHelper;
```

`Type` — numeric type for frequency calculations (`float`, `IQ` fixed-point, etc.). Must satisfy the `RealType` concept.

## Constructors

```cpp
ITIMHelper();
ITIMHelper(ATIM& adapter);
ITIMHelper(ATIM& adapter, ATIM::ChannelOption channel);
ITIMHelper(TimerChannel& timerChannel);
```

## API

| Method | Return | Description |
|--------|--------|-------------|
| `SetState(bool)` | `ITIMHelper&` | Enable/disable the channel output based on its configured polarity (positive, negative, or both) |
| `SetDivision(ClockDivisionOption)` | `void` | Set clock division |
| `SetPrescaler(uint32)` | `void` | Set prescaler value |
| `SetPeriod(uint32)` | `void` | Set auto-reload period |
| `SetCompare(uint32)` | `void` | Set compare value for the channel |
| `SetCompareMode(OutputCompareOption)` | `void` | Set output compare mode for the channel |
| `GetCapture()` | `uint32` | Read capture value from the channel |
| `GetAdapter()` | `ATIM*` | Get the underlying timer adapter |
| `GetChannel()` | `ChannelOption` | Get the configured channel |
| `GetChannelNumber()` | `uint8` | Get the 1-based channel number |
| `SetBaseFrequency(Type)` | `void` | Calculate and apply prescaler + period for a target frequency |

## Frequency Calculation

`SetBaseFrequency` automatically computes optimal prescaler and period values from the timer's bus clock:

```cpp
ITIMHelper<float> helper(tim2, ATIM::ChannelOption::Channel1);
helper.SetBaseFrequency(1000.0f); // 1 kHz
```

For 16-bit timers, it searches for a prescaler that brings the period into the 16-bit range. For 32-bit timers, it uses prescaler 0 and a large period directly.

The update is performed inside a critical section to avoid glitches.

## Channel Enable Logic

`SetState(true/false)` respects the channel's configured polarity (`ChannelSelect`):

| ChannelSelect | Enable | Disable |
|--------------|--------|---------|
| `Positive` | `EnablePositive` | `DisablePositive` |
| `Negative` | `EnableNegative` | `DisableNegative` |
| `PositiveNegative` | `EnableAll` | `DisableAll` |
| `NoOutput` | `DisableAll` | `DisableAll` |
