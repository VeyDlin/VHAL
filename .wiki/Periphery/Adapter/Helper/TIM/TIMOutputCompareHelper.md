# TIMOutputCompareHelper

Extends `ITIMHelper` with output compare features: set frequency and duty cycle independently, tune prescaler for a specific frequency, and read back current frequency/duty info. Primary use case — PWM generation with runtime control.

Header: `#include <Adapter/Helper/TIM/TIMOutputCompareHelper.h>`

## Template Parameter

```cpp
template <RealType Type = float>
class TIMOutputCompareHelper : public ITIMHelper<Type>;
```

## Constructors

```cpp
TIMOutputCompareHelper();
TIMOutputCompareHelper(ATIM& adapter, ATIM::ChannelOption channel);
TIMOutputCompareHelper(TimerChannel& timerChannel);
```

## FrequencyInfo

```cpp
struct FrequencyInfo {
    Type frequencyHz;
    Type duty; // in %
};
```

## API

| Method | Return | Description |
|--------|--------|-------------|
| `SetFrequencyInfo(FrequencyInfo)` | `TIMOutputCompareHelper&` | Set both frequency and duty cycle at once |
| `SetFrequency(Type)` | `TIMOutputCompareHelper&` | Change frequency, keeping current duty |
| `SetDuty(Type)` | `TIMOutputCompareHelper&` | Change duty cycle (%), keeping current frequency |
| `GetFrequencyInfo()` | `FrequencyInfo` | Read back current frequency and duty from registers |
| `TunePrescaler(Type)` | `TIMOutputCompareHelper&` | Adjust prescaler for target frequency without changing period |
| `SetHalfCompare()` | `TIMOutputCompareHelper&` | Set compare to half of current period (50% duty) |
| `TuneHalfCompare(uint32)` | `TIMOutputCompareHelper&` | Shortcut: `TunePrescaler(freq*2)` + `SetHalfCompare()` |
| `EnableCounter(bool)` | `TIMOutputCompareHelper&` | Enable/disable the timer counter |

All setters return `*this` for chaining.

## Usage

### PWM with frequency and duty

```cpp
TIMOutputCompareHelper<float> pwm(tim2, ATIM::ChannelOption::Channel1);

// Set 10 kHz, 25% duty
pwm.SetFrequencyInfo({ .frequencyHz = 10000.0f, .duty = 25.0f });
pwm.SetState(true);

// Change duty at runtime
pwm.SetDuty(50.0f);

// Read back
auto info = pwm.GetFrequencyInfo();
// info.frequencyHz ≈ 10000.0
// info.duty ≈ 50.0
```

### Square wave (50% duty)

```cpp
TIMOutputCompareHelper<float> wave(tim3, ATIM::ChannelOption::Channel2);
wave.TuneHalfCompare(1000); // 1 kHz square wave
wave.SetState(true);
```

### Method chaining

```cpp
pwm.SetFrequency(20000.0f)
   .SetDuty(75.0f)
   .SetState(true)
   .EnableCounter(true);
```

## Notes

- `SetFrequencyInfo` applies prescaler, period, and compare inside a critical section to avoid glitches
- `SetFrequency` and `SetDuty` call `GetFrequencyInfo()` first to preserve the other parameter, then call `SetFrequencyInfo` — two register reads + one atomic update
- For 32-bit timers, prescaler is 0 and period is large; for 16-bit timers, the prescaler is computed to fit period into 16 bits
