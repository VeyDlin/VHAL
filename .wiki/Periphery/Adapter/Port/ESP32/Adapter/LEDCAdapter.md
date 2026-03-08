# LEDCAdapter

Abstract adapter for the ESP32 LEDC (LED Control) PWM peripheral. Provides multi-channel PWM generation with configurable frequency, duty resolution, and hardware fade support. Ideal for LED dimming and simple motor control.

Header: `#include <Adapter/Port/ESP32/Adapter/LEDCAdapter.h>`

> **Note:** This adapter does not use a template parameter — ESP-IDF manages LEDC handles internally.

## Option Types

| Type | Base | Description |
|------|------|-------------|
| `TimerOption` | `IOption<uint32>` | LEDC timer index |
| `SpeedModeOption` | `IOption<uint32>` | Speed mode (high/low) |
| `ChannelOption` | `IOption<uint32>` | LEDC channel index |
| `ResolutionOption` | `IOption<uint32>` | Duty resolution in bits |

## Parameters

```cpp
struct Parameters {
    uint32 frequencyHz = 5000;
    ResolutionOption dutyResolution;
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `frequencyHz` | `5000` | PWM frequency |
| `dutyResolution` | — | Duty cycle bit width (e.g., 13 bits = 0..8191) |

## Channel Config

```cpp
struct ChannelConfig {
    ChannelOption channel;
    int pin = -1;
    uint32 duty = 0;
    uint32 hpoint = 0;
};
```

## Constructor

```cpp
LEDCAdapter(TimerOption timer, SpeedModeOption speedMode);
```

## API

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(Parameters val)` | `ResultStatus` | Configure timer frequency and resolution |
| `AddChannel(ChannelConfig config)` | `ResultStatus` | Bind a channel to a GPIO pin |
| `SetDuty(ChannelOption channel, uint32 duty)` | `ResultStatus` | Set raw duty cycle value |
| `SetDutyPercent(ChannelOption channel, float percent)` | `ResultStatus` | Set duty as percentage (0–100) |
| `SetFrequency(uint32 frequencyHz)` | `ResultStatus` | Change PWM frequency |
| `GetFrequency()` | `uint32` | Get current frequency |
| `GetDuty(ChannelOption channel)` | `uint32` | Get current raw duty |
| `GetMaxDuty()` | `uint32` | Get maximum duty value for current resolution |
| `FadeTo(ChannelOption ch, uint32 targetDuty, uint32 fadeTimeMs)` | `ResultStatus` | Hardware fade to target duty |
| `Stop(ChannelOption channel, uint32 idleLevel)` | `ResultStatus` | Stop channel output |

## Callbacks

| Callback | Type | Description |
|----------|------|-------------|
| `onFadeEnd` | `std::function<void(ChannelOption)>` | Hardware fade completed |

## Usage Example

```cpp
LEDCAdapter ledPwm(LEDCAdapter::Timer::Timer0, LEDCAdapter::SpeedMode::HighSpeed);

ledPwm.SetParameters({
    .frequencyHz = 5000,
    .dutyResolution = LEDCAdapter::Resolution::B13  // 13-bit: 0..8191
});

ledPwm.AddChannel({
    .channel = LEDCAdapter::Channel::Ch0,
    .pin = 18,
    .duty = 0
});

// Set brightness to 50%
ledPwm.SetDutyPercent(LEDCAdapter::Channel::Ch0, 50.0f);

// Smooth fade to 100%
ledPwm.FadeTo(LEDCAdapter::Channel::Ch0, ledPwm.GetMaxDuty(), 1000);
```
