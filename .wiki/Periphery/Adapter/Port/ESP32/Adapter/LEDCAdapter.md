# LEDCAdapter

Adapter for the ESP32 LEDC (LED Control) PWM peripheral. Provides multi-channel PWM generation with configurable frequency, duty resolution, and hardware fade support. Ideal for LED dimming and simple motor control.

Header: `#include <Adapter/Port/ESP32/Common/LEDCAdapterESP.h>` (class `ALEDC`, implementation of `LEDCAdapter`, which is declared in `#include <Adapter/Port/ESP32/Adapter/LEDCAdapter.h>`)

> **Note:** This adapter does not use a template parameter. ESP-IDF manages LEDC handles internally.

## Option Types

| Type | Base | Description |
|------|------|-------------|
| `TimerOption` | `IOption<uint32>` | LEDC timer index |
| `SpeedModeOption` | `IOption<uint32>` | Speed mode |
| `ChannelOption` | `IOption<uint32>` | LEDC channel index |
| `ResolutionOption` | `IOption<uint32>` | Duty resolution in bits |

## Option Values

Values are accessed as static members of `ALEDC`. Groups marked with a capability exist only when the chip defines it (`soc/soc_caps.h`, included by the ESP32 `Platform.h`).

| Group | Members | Available when |
|-------|---------|----------------|
| `ALEDC::Timer` | `T0`, `T1`, `T2`, `T3` | Always |
| `ALEDC::SpeedMode` | `Low` | Always |
| `ALEDC::SpeedMode` | `High` | `SOC_LEDC_SUPPORT_HS_MODE` |
| `ALEDC::Channel` | `C0`, `C1`, `C2`, `C3`, `C4`, `C5` | Always |
| `ALEDC::Channel` | `C6`, `C7` | `SOC_LEDC_CHANNEL_NUM > 6` |
| `ALEDC::Resolution` | `B1` to `B14` | Always |
| `ALEDC::Resolution` | `B15` to `B20` | `SOC_LEDC_TIMER_BIT_WIDTH > 14` |

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
| `dutyResolution` | — | Duty cycle bit width, for example `ALEDC::Resolution::B13` (0..8191). Must be set |

## Channel Config

```cpp
struct ChannelConfig {
    ChannelOption channel;
    int pin = -1;
    uint32 duty = 0;
    uint32 hpoint = 0;
    bool invert = false;
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `channel` | — | Channel to bind. Must be set |
| `pin` | `-1` | GPIO number of the output |
| `duty` | `0` | Initial raw duty |
| `hpoint` | `0` | Phase point of the rising edge, used for every duty update of the channel |
| `invert` | `false` | Invert the output signal |

## Constructor

```cpp
ALEDC(TimerOption timer, SpeedModeOption speedMode);
```

## API

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(Parameters val)` | `ResultStatus` | Configure the timer frequency and duty resolution. Clears the channel list, so call `AddChannel()` afterwards |
| `AddChannel(ChannelConfig config)` | `ResultStatus` | Bind a channel to a GPIO pin. Adding a channel that is already bound reconfigures it. Up to 8 channels per adapter |
| `SetDuty(ChannelOption channel, uint32 duty)` | `ResultStatus` | Set raw duty cycle value |
| `SetDutyPercent(ChannelOption channel, float percent)` | `ResultStatus` | Set duty as percentage (0–100), computed from `GetMaxDuty()` |
| `SetFrequency(uint32 frequencyHz)` | `ResultStatus` | Change PWM frequency of the timer |
| `GetFrequency()` | `uint32` | Get current frequency |
| `GetDuty(ChannelOption channel)` | `uint32` | Get current raw duty |
| `GetMaxDuty()` | `uint32` | Get `1 << dutyResolution` for the current resolution |
| `FadeTo(ChannelOption channel, uint32 targetDuty, uint32 fadeTimeMs)` | `ResultStatus` | Start a hardware fade to the target duty. Returns without waiting for the fade to finish |
| `Stop(ChannelOption channel, uint32 idleLevel = 0)` | `ResultStatus` | Stop channel output and hold the idle level |

The destructor stops all bound channels with idle level `0`.

## Callbacks

| Callback | Type | Description |
|----------|------|-------------|
| `onFadeEnd` | `std::function<void(ChannelOption channel)>` | Hardware fade started by `FadeTo()` completed. Called in interrupt context |

Assign `onFadeEnd` before the first `FadeTo()` on a channel: the fade-end callback is registered with the driver only when a handler is set at that moment.

The LEDC fade interrupt is not masked while flash memory is being written (NVS, OTA, file systems), and the handler is ordinary code in flash. Do not let a fade with `onFadeEnd` set complete during a flash write.

## Usage Example

```cpp
ALEDC pwm(ALEDC::Timer::T0, ALEDC::SpeedMode::Low);

pwm.SetParameters({
    .frequencyHz = 5000,
    .dutyResolution = ALEDC::Resolution::B13  // 13-bit: 0..8191
});

pwm.AddChannel({
    .channel = ALEDC::Channel::C0,
    .pin = 18,
    .duty = 0
});

// Set brightness to 50%
pwm.SetDutyPercent(ALEDC::Channel::C0, 50.0f);

// Smooth fade to full scale over 1000 ms
pwm.onFadeEnd = [](ALEDC::ChannelOption channel) {
    // Interrupt context: keep the handler short
};
pwm.FadeTo(ALEDC::Channel::C0, 8191, 1000);
```
