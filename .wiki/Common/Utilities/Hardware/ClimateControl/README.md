# ClimateControl

A temperature control library providing bang-bang (on/off) and PID-based climate regulation, running as RTOS threads.

## Table of Contents

- [Quick Start](#quick-start)
- [IClimateControl (Base Class)](#iclimatcontrol-base-class)
  - [Work Modes](#work-modes)
  - [Control Modes](#control-modes)
  - [Callbacks](#callbacks)
  - [Manual Control](#manual-control)
- [SwitchClimateControl](#switchclimatecontrol)
- [PidClimateControl](#pidclimatecontrol)
  - [PID Tuning](#pid-tuning)
  - [Stabilization Detection](#stabilization-detection)
- [API Reference](#api-reference)

## Quick Start

```cpp
#include <Utilities/Hardware/ClimateControl/SwitchClimateControl.h>

SwitchClimateControl climate;  // defaults to float

// Called periodically by the control loop; receives the control output,
// should return the current temperature reading.
climate.onUpdateState = [](float dirty) -> float {
    if (dirty > 0) {
        heater.On();
    } else {
        heater.Off();
    }
    return temperatureSensor.Read();
};

// Called once when the target temperature is reached
climate.onHold = []() {
    LED::Green.On();
};

climate.SetHoldTemperature(60.0f);
climate.SetControlMode(IClimateControl<>::ControlMode::Heating);
climate.SetWorkMode(IClimateControl<>::WorkMode::Auto);
climate.Start();
```

## IClimateControl (Base Class)

`template <RealType Type = float>`

`IClimateControl` inherits from `Thread<128>` and provides the shared interface for all climate controllers.

### Work Modes

| Mode       | Description                                          |
|------------|------------------------------------------------------|
| `Disable`  | Controller thread runs but takes no action (default) |
| `Auto`     | Automatic closed-loop temperature regulation         |
| `Manual`   | User directly sets control output via methods        |

```cpp
climate.SetWorkMode(IClimateControl<>::WorkMode::Auto);
auto mode = climate.GetWorkMode();
```

### Control Modes

| Mode             | Description                                   |
|------------------|-----------------------------------------------|
| `Heating`        | Only positive output allowed (heating only)   |
| `Cooling`        | Only negative output allowed (cooling only)   |
| `HeatingCooling` | Both positive and negative output (default)   |

```cpp
climate.SetControlMode(IClimateControl<>::ControlMode::Cooling);
```

### Callbacks

**`onUpdateState`** -- Called each control cycle. Receives the control output (`dirty`) as a float and must return the current temperature.

- In `SwitchClimateControl`: `dirty` is `1`, `-1`, or `0`.
- In `PidClimateControl`: `dirty` is a continuous value in the configured output range.

```cpp
climate.onUpdateState = [](float dirty) -> float {
    pwmHeater.SetDuty(dirty);       // apply control signal
    return adcSensor.ReadCelsius();  // return current temperature
};
```

**`onHold`** -- Called once when the current temperature first reaches the hold target.

```cpp
climate.onHold = []() {
    buzzer.Beep();
};
```

### Manual Control

When `WorkMode::Manual` is active, the automatic loop is paused and you can push values directly:

```cpp
climate.SetWorkMode(IClimateControl<>::WorkMode::Manual);
climate.SetManualDirty(0.75f);  // directly sets output via onUpdateState
climate.SetManualHold();        // triggers onHold callback once
```

### Time Step

Controls how often the regulation loop runs (default: 1 second):

```cpp
climate.SetTimeStep(std::chrono::milliseconds(500));
```

## SwitchClimateControl

`template <RealType Type = float>`

A simple bang-bang controller. Each cycle it outputs a discrete signal:

| Condition (Heating)         | Output |
|-----------------------------|--------|
| `currentTemp < holdTemp`    | `1`    |
| `currentTemp >= holdTemp`   | `0`    |

| Condition (Cooling)         | Output |
|-----------------------------|--------|
| `currentTemp > holdTemp`    | `-1`   |
| `currentTemp <= holdTemp`   | `0`    |

In `HeatingCooling` mode, output is `1` when below target and `-1` when above.

```cpp
#include <Utilities/Hardware/ClimateControl/SwitchClimateControl.h>

SwitchClimateControl climate;
climate.temperatureTolerance = 1.0f; // tolerance for hold detection in HeatingCooling mode

climate.onUpdateState = [](float dirty) -> float {
    relay.Set(dirty > 0);
    return sensor.Read();
};

climate.SetHoldTemperature(25.0f);
climate.SetControlMode(IClimateControl<>::ControlMode::Heating);
climate.SetWorkMode(IClimateControl<>::WorkMode::Auto);
climate.Start();
```

## PidClimateControl

`template <RealType Type = float>`

A PID-based controller that produces a continuous output signal for smooth temperature regulation.

```cpp
#include <Utilities/Hardware/ClimateControl/PidClimateControl.h>

PidClimateControl climate;

climate.SetPidCoefficients(2.0f, 0.5f, 0.1f);
climate.SetOutputLimits(-1.0f, 1.0f);
climate.SetIntegratorLimit(true, 0.3f);

climate.onUpdateState = [](float dirty) -> float {
    pwm.SetDuty(dirty);
    return sensor.Read();
};

climate.SetHoldTemperature(80.0f);
climate.SetControlMode(IClimateControl<>::ControlMode::Heating);
climate.SetWorkMode(IClimateControl<>::WorkMode::Auto);
climate.Start();
```

Default PID configuration:
- Coefficients: P=1.0, I=0.1, D=0.05
- Output range: -1.0 to 1.0
- Integrator windup limit: +/-0.5

### PID Tuning

```cpp
climate.SetPidCoefficients(5.0f, 0.2f, 0.8f);  // P, I, D
climate.SetOutputLimits(-1.0f, 1.0f);           // min, max
climate.SetIntegratorLimit(true, 0.4f);          // enable, symmetric limit
```

### Stabilization Detection

PID mode supports an optional stabilization event that fires when the error stays within a tolerance for a given duration:

```cpp
climate.SetStabilizationDetection(
    0.5f,   // error tolerance (degrees)
    5000    // time the error must stay within tolerance (ms)
);
```

The stabilization callback triggers `onHold` when conditions are met.

## API Reference

### IClimateControl

| Method | Description |
|--------|-------------|
| `Start()` | Creates the RTOS thread and begins the control loop |
| `SetHoldTemperature(float)` | Sets the target temperature |
| `GetHoldTemperature() -> float` | Returns the current target temperature |
| `SetWorkMode(WorkMode)` | Sets `Disable`, `Manual`, or `Auto` |
| `GetWorkMode() -> WorkMode` | Returns the current work mode |
| `SetControlMode(ControlMode)` | Sets `Heating`, `Cooling`, or `HeatingCooling` |
| `GetControlMode() -> ControlMode` | Returns the current control mode |
| `SetTimeStep(milliseconds)` | Sets the control loop period |
| `GetTimeStep() -> milliseconds` | Returns the control loop period |
| `SetManualDirty(float)` | Sends a manual output value (Manual mode only) |
| `SetManualHold()` | Triggers `onHold` callback once (Manual mode only) |

### SwitchClimateControl

| Member | Description |
|--------|-------------|
| `temperatureTolerance` | Float, tolerance for hold detection in `HeatingCooling` mode (default: 0.5) |

### PidClimateControl

| Method | Description |
|--------|-------------|
| `SetPidCoefficients(float p, float i, float d)` | Sets PID gains |
| `SetOutputLimits(float min, float max)` | Sets output value range |
| `SetIntegratorLimit(bool enable, float limit)` | Sets symmetric integrator anti-windup limit |
| `SetStabilizationDetection(float errorTolerance, uint32 timeMs)` | Configures stabilization event |
| `temperatureTolerance` | Static constexpr float, tolerance for hold detection (0.5) |
