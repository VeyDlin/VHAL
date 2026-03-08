# PidController

A template-based PID (Proportional-Integral-Derivative) controller with support for cyclic input (angular systems), dead zones, output filtering, integrator anti-windup, and stabilization/destabilization event detection.

---

## Table of Contents

- [Quick Start](#quick-start)
- [Template Parameter](#template-parameter)
- [Configuration Structures](#configuration-structures)
  - [Input](#input)
  - [Output](#output)
  - [Coefficients](#coefficients)
  - [IntegratorLimit](#integratorlimit)
  - [Roll (Cyclic Input)](#roll-cyclic-input)
  - [RollDeadZone](#rolldeadzone)
  - [Filter](#filter)
  - [StabilizedEvent](#stabilizedevent)
  - [DestabilizedEvent](#destabilizedevent)
- [API Reference](#api-reference)
  - [Construction and Core](#construction-and-core)
  - [Input Setters](#input-setters)
  - [Coefficient Setters](#coefficient-setters)
  - [Output Setters](#output-setters)
  - [Integrator Limit Setters](#integrator-limit-setters)
  - [Roll and Dead Zone Setters](#roll-and-dead-zone-setters)
  - [Filter Setters](#filter-setters)
  - [Stabilization Event Setters](#stabilization-event-setters)
  - [Destabilization Event Setters](#destabilization-event-setters)
  - [State Control](#state-control)
  - [Getters](#getters)
- [Usage Examples](#usage-examples)
  - [Basic PID Control](#basic-pid-control)
  - [Angular Position Control with Roll](#angular-position-control-with-roll)
  - [Filtered PID with Anti-Windup](#filtered-pid-with-anti-windup)
  - [Stabilization and Destabilization Events](#stabilization-and-destabilization-events)
  - [Fluent API (Method Chaining)](#fluent-api-method-chaining)

---

## Quick Start

```cpp
#include <Utilities/Math/PID/PidController.h>

// Create a PID controller with P=1.0, I=0.5, D=0.1, output range [-10, 10]
PidController<> pid({1.0f, 0.5f, 0.1f}, {10.0f, -10.0f});

// In your control loop (called at a fixed frequency):
pid.SetFeedback(sensorValue, 1000); // feedback value, 1000 Hz
pid.SetReference(targetValue);       // desired setpoint
pid.Resolve();                       // compute PID output

float controlSignal = pid.Get();     // read the result
```

---

## Template Parameter

```cpp
template <typename Type = float>
class PidController;
```

`Type` must be `float`, `double`, or `long double`. Defaults to `float`.

```cpp
PidController<>      pidFloat;   // float (default)
PidController<double> pidDouble; // double precision
```

---

## Configuration Structures

### Input

Defines the feedback value, reference (setpoint), and sampling frequency.

```cpp
struct Input {
    Type feedback = 0;    // current measured value
    Type reference = 0;   // target setpoint
    uint32 frequency = 1; // sampling frequency in Hz (how often Resolve() is called)
};
```

### Output

Defines output signal clamping and optional inversion.

```cpp
struct Output {
    Type max = 1;           // maximum output value
    Type min = 0;           // minimum output value
    bool inversion = false; // if true, error = feedback - reference (instead of reference - feedback)
};
```

### Coefficients

The three PID gains.

```cpp
struct Coefficients {
    Type proportional = 1; // P gain
    Type integral = 0;     // I gain
    Type derivative = 0;   // D gain
};
```

### IntegratorLimit

Clamps the integral accumulator to prevent windup.

```cpp
struct IntegratorLimit {
    bool enable = false; // enable integrator clamping
    Type max = 1;        // upper limit
    Type min = 0;        // lower limit
};
```

### Roll (Cyclic Input)

Enables cyclic/wrapping input handling (e.g., 0-360 degree angles).

```cpp
struct Roll {
    bool enable = false; // enable cyclic input
    Type maxInput = 1;   // upper boundary of cyclic range
    Type minInput = 0;   // lower boundary of cyclic range
};
```

When enabled, the controller computes the shortest-path error across the wrap boundary. For example, with a 0-360 range, an error from 350 to 10 degrees is computed as +20 rather than -340.

### RollDeadZone

Defines a forbidden zone within the cyclic range that the reference is clamped away from.

```cpp
struct RollDeadZone {
    bool enable = false;           // enable dead zone
    bool throughConnection = false; // if true, reference outside the dead zone is clamped to the nearest edge
    Type start = 0;                // dead zone start
    Type end = 0;                  // dead zone end
};
```

When `throughConnection` is `false`, the controller also avoids routing the error path through the dead zone, reversing direction if needed.

### Filter

Enables an advanced filtering mode with back-calculation anti-windup on the integrator and a first-order filter on the derivative.

```cpp
struct Filter {
    bool enable = false;     // enable filtered PID mode
    Type backSaturation = 1; // back-calculation coefficient for integrator anti-windup
    Type derivative = 1;     // derivative filter coefficient
};
```

### StabilizedEvent

Fires a callback when the error stays within a defined band for a specified duration.

```cpp
struct StabilizedEvent {
    bool enable = false;
    Type errorMax = 1;       // upper error threshold
    Type errorMin = 0;       // lower error threshold
    Type timeMs = 100;       // required hold time in milliseconds
    std::function<void(PidController& self)> onStabilized = nullptr;
};
```

### DestabilizedEvent

Fires a callback when the error stays outside a defined band for a specified duration.

```cpp
struct DestabilizedEvent {
    bool enable = false;
    Type errorMax = 1;       // upper error threshold
    Type errorMin = 0;       // lower error threshold
    Type timeMs = 100;       // required hold time in milliseconds
    std::function<void(PidController& self)> onDestabilized = nullptr;
};
```

---

## API Reference

All setter methods return `PidController&`, enabling method chaining.

### Construction and Core

| Method | Description |
|--------|-------------|
| `PidController()` | Default constructor |
| `PidController(Coefficients, Output)` | Construct with gains and output limits |
| `Resolve()` | Compute one PID iteration |
| `Reset()` | Zero all internal state (integrator, error, derivative, output) |

### Input Setters

| Method | Description |
|--------|-------------|
| `SetInpit(Input)` | Set feedback, reference, and frequency at once |
| `SetReference(Type)` | Set the target setpoint |
| `SetFeedback(Type)` | Set the feedback value |
| `SetFeedback(Type, uint32)` | Set feedback and frequency together |
| `SetFrequency(uint32)` | Set the sampling frequency in Hz |

### Coefficient Setters

| Method | Description |
|--------|-------------|
| `SetCoefficients(Coefficients)` | Set all three PID gains |
| `SetProportional(Type)` | Set P gain |
| `SetIntegral(Type)` | Set I gain |
| `SetDerivative(Type)` | Set D gain |

### Output Setters

| Method | Description |
|--------|-------------|
| `SetOuput(Output)` | Set output limits and inversion |
| `SetOuputInversion(bool)` | Enable/disable output inversion |
| `SetPidOutputValue(Type)` | Directly override the current output value |

### Integrator Limit Setters

| Method | Description |
|--------|-------------|
| `SetIntegratorLimit(IntegratorLimit)` | Set full integrator limit config |
| `SetIntegratorLimit(Type)` | Set symmetric limit (+-val) |
| `SetIntegratorLimitEnable(bool)` | Enable/disable integrator limiting |
| `SetIntegratorLimitMin(Type)` | Set lower limit |
| `SetIntegratorLimitMax(Type)` | Set upper limit |
| `SetIntegratorLimitMinMax(Type)` | Set symmetric limits (+-val) |
| `SetIntegratorLimitMinMax(Type, Type)` | Set asymmetric limits (min, max) |

### Roll and Dead Zone Setters

| Method | Description |
|--------|-------------|
| `SetRoll(Roll)` | Configure cyclic input handling |
| `SetRollEnable(bool)` | Enable/disable cyclic input |
| `SetRollDeadZone(RollDeadZone)` | Configure dead zone (start/end are auto-sorted) |

### Filter Setters

| Method | Description |
|--------|-------------|
| `SetFilter(Filter)` | Set full filter configuration |
| `SetFilterEnable(bool)` | Enable/disable filtered PID mode |
| `SetFilterBackSaturation(Type)` | Set back-calculation anti-windup coefficient |
| `SetFilterDerivative(Type)` | Set derivative filter coefficient |

### Stabilization Event Setters

| Method | Description |
|--------|-------------|
| `SetStabilizedEvent(StabilizedEvent)` | Set full stabilization event config |
| `SetStabilizedEventEnable(bool)` | Enable/disable the event |
| `SetStabilizedEventTime(uint32)` | Set hold time in ms |
| `SetStabilizedEventMin(Type)` | Set lower error threshold |
| `SetStabilizedEventMax(Type)` | Set upper error threshold |
| `SetStabilizedEventMinMax(Type)` | Set symmetric error band (+-val) |
| `SetStabilizedEventMinMax(Type, Type)` | Set asymmetric error band |
| `SetStabilizedEventHandle(callback)` | Set the stabilization callback |

### Destabilization Event Setters

| Method | Description |
|--------|-------------|
| `SetDestabilizedEvent(DestabilizedEvent)` | Set full destabilization event config |
| `SetDestabilizedEventEnable(bool)` | Enable/disable the event |
| `SetDestabilizedEventTime(uint32)` | Set hold time in ms |
| `SetDestabilizedEventMin(Type)` | Set lower error threshold |
| `SetDestabilizedEventMax(Type)` | Set upper error threshold |
| `SetDestabilizedEventMinMax(Type)` | Set symmetric error band (+-val) |
| `SetDestabilizedEventMinMax(Type, Type)` | Set asymmetric error band |
| `SetDestabilizedEventHandle(callback)` | Set the destabilization callback |

### State Control

| Method | Description |
|--------|-------------|
| `SetFrozen(bool)` | When true, `Resolve()` becomes a no-op (output is held) |

### Getters

| Method | Returns |
|--------|---------|
| `Get()` | Current output value |
| `GetLastError()` | Last computed error |

---

## Usage Examples

### Basic PID Control

```cpp
PidController<> pid({2.0f, 0.5f, 0.1f}, {100.0f, -100.0f});

pid.SetFrequency(1000); // 1 kHz control loop

// Control loop
void controlLoop(float measuredValue, float setpoint) {
    pid.SetFeedback(measuredValue);
    pid.SetReference(setpoint);
    pid.Resolve();

    float output = pid.Get();
    applyToActuator(output);
}
```

### Angular Position Control with Roll

```cpp
PidController<> pid({3.0f, 0.2f, 0.05f}, {100.0f, -100.0f});

// Enable cyclic input for 0-360 degree range
pid.SetRoll({true, 360.0f, 0.0f});

// Optionally add a dead zone (e.g., avoid the 85-95 degree region)
pid.SetRollDeadZone({true, false, 85.0f, 95.0f});

pid.SetFrequency(500);

// Now error from 350 to 10 degrees is correctly computed as +20, not -340
pid.SetFeedback(350.0f);
pid.SetReference(10.0f);
pid.Resolve();

float output = pid.Get();
```

### Filtered PID with Anti-Windup

```cpp
PidController<> pid;

pid.SetCoefficients({1.0f, 0.5f, 0.1f});
pid.SetOuput({10.0f, -10.0f});
pid.SetFrequency(1000);

// Enable filtering with back-calculation anti-windup
pid.SetFilter({true, 0.8f, 0.2f});

// Also clamp the integrator directly
pid.SetIntegratorLimit({true, 5.0f, -5.0f});

pid.SetFeedback(sensorValue);
pid.SetReference(target);
pid.Resolve();
```

### Stabilization and Destabilization Events

```cpp
PidController<> pid({1.0f, 0.5f, 0.1f}, {10.0f, -10.0f});
pid.SetFrequency(1000);

// Fire callback when error stays within [-0.1, 0.1] for 500 ms
pid.SetStabilizedEvent({
    .enable = true,
    .errorMax = 0.1f,
    .errorMin = -0.1f,
    .timeMs = 500,
    .onStabilized = [](PidController<>& self) {
        // System has reached and held the setpoint
        signalReady();
    }
});

// Fire callback when error exceeds [-0.5, 0.5] for 300 ms
pid.SetDestabilizedEvent({
    .enable = true,
    .errorMax = 0.5f,
    .errorMin = -0.5f,
    .timeMs = 300,
    .onDestabilized = [](PidController<>& self) {
        // System has lost tracking
        signalAlarm();
    }
});
```

### Fluent API (Method Chaining)

All setters return `*this`, so configuration can be chained:

```cpp
PidController<> pid;

pid.SetCoefficients({2.0f, 1.0f, 0.05f})
   .SetOuput({100.0f, -100.0f})
   .SetFrequency(1000)
   .SetFilterEnable(true)
   .SetFilterBackSaturation(0.8f)
   .SetFilterDerivative(0.3f)
   .SetIntegratorLimit({true, 50.0f, -50.0f})
   .SetRoll({true, 360.0f, 0.0f});
```
