# Filter

A collection of lightweight signal filtering utilities for embedded systems.

## Table of Contents

- [Quick Start](#quick-start)
- [Hysteresis](#hysteresis)
- [KalmanFilter](#kalmanfilter)
- [LowPassFilter](#lowpassfilter)
- [API Reference](#api-reference)

## Quick Start

```cpp
#include <Utilities/Math/Filter/KalmanFilter.h>
#include <Utilities/Math/Filter/LowPassFilter.h>
#include <Utilities/Math/Filter/Hysteresis.h>

// Smooth a noisy sensor reading with a Kalman filter
KalmanFilter kf(0.01f, 3.0f, 1.0f, 0.0f, 1.0f);
float filtered = kf.Filter(rawSensorValue, 0.0f);

// Simple low-pass filter
LowPassFilter lpf(10.0f, 1.0f);
float smooth = lpf.Set(rawValue).Resolve().Get();

// Hysteresis to suppress jitter
Hysteresis<float> hyst(5.0f);
float stable = hyst.Get(rawValue);

// With IQ fixed-point math
KalmanFilter<iq16> kfIQ(0.01f, 3.0f, 1.0f, 0.0f, 1.0f);
LowPassFilter<iq16> lpfIQ(10.0f, 1.0f);
```

## Hysteresis

`template <typename ValueType = float>`

A template class that implements adaptive hysteresis with an adjustable window. It maintains a range defined by a half-gap around the current value. When input crosses the window boundaries, the window shifts. An optional directional pull mode allows one-sided tracking.

### PullType Enum

| Value      | Behavior |
|------------|----------|
| `NoPull`   | Window adjusts symmetrically when the value moves outside either bound |
| `PullUp`   | Window tracks upward movement even within bounds |
| `PullDown` | Window tracks downward movement even within bounds |

### Usage

```cpp
// Basic hysteresis with a half-gap of 2.0 (total dead-band of 4.0)
Hysteresis<float> hyst(2.0f);

hyst.Get(10.0f);  // Initializes window to [8, 12], returns 10
hyst.Get(11.0f);  // Within window, returns 10 (unchanged)
hyst.Get(13.0f);  // Exceeds upper bound, window shifts to [9, 13], returns 13

// Pull-up mode: tracks every upward change
Hysteresis<int> hystUp(3, Hysteresis<int>::PullType::PullUp);
hystUp.Get(50);   // returns 50
hystUp.Get(51);   // pulled up immediately, returns 51
hystUp.Get(49);   // within window, returns 51

// Pull-down mode: tracks every downward change
Hysteresis<float> hystDown(1.0f, Hysteresis<float>::PullType::PullDown);
hystDown.Get(100.0f);  // returns 100
hystDown.Get(99.5f);   // pulled down immediately, returns 99.5
hystDown.Get(100.5f);  // within window, returns 99.5
```

### Constructor

```cpp
Hysteresis(ValueType halfGap, PullType pull = PullType::NoPull);
```

- `halfGap` -- half of the dead-band width (must be >= 0).
- `pull` -- directional pull behavior (default: `NoPull`).

### Methods

| Method | Description |
|--------|-------------|
| `ValueType Get(ValueType value)` | Processes a new input value. Returns the filtered (stable) output. |

## KalmanFilter

`template <RealType Type = float>`

A scalar (1D) Kalman filter suitable for smoothing noisy sensor data with an optional control input.

### Constructor

```cpp
KalmanFilter(
    Type processNoise,        // R - process noise covariance
    Type measurementNoise,    // Q - measurement noise covariance
    Type stateTransition,     // A - state transition coefficient
    Type controlInputEffect,  // B - control input coefficient
    Type measurementMapping   // C - measurement mapping coefficient
);
```

### Usage

```cpp
// Typical sensor filtering (no control input)
KalmanFilter kf(0.01f, 3.0f, 1.0f, 0.0f, 1.0f);

// In a periodic loop:
float filtered = kf.Filter(readADC(), 0.0f);

// With control input (e.g., motor model with known acceleration)
KalmanFilter motorFilter(0.02f, 1.0f, 1.0f, 0.5f, 1.0f);
float state = motorFilter.Filter(encoderReading, appliedVoltage);

// Retrieve last estimate without new measurement
float last = kf.GetLastStateEstimate();

// Tune noise parameters at runtime
kf.SetProcessNoise(0.05f);
kf.SetMeasurementNoise(2.0f);
```

### Methods

| Method | Description |
|--------|-------------|
| `Type Filter(Type measurement, Type controlInput)` | Runs one predict-correct cycle. Returns the updated state estimate. |
| `Type GetLastStateEstimate() const` | Returns the most recent state estimate without processing new data. |
| `void SetMeasurementNoise(Type noise)` | Updates the measurement noise covariance (Q) at runtime. |
| `void SetProcessNoise(Type noise)` | Updates the process noise covariance (R) at runtime. |

## LowPassFilter

`template <RealType Type = float>`

A first-order IIR (infinite impulse response) low-pass filter. Configured by a frequency time constant and a sampling time. Uses a fluent (chained) API.

### Constructor

```cpp
LowPassFilter(Type frequencyTime, Type samplingTime);
```

- `frequencyTime` -- inverse of the cutoff frequency (1 / f_cutoff).
- `samplingTime` -- the sampling period of the input signal.

### Usage

```cpp
// Create filter: frequencyTime=10, samplingTime=1
LowPassFilter lpf(10.0f, 1.0f);

// Process a single sample (fluent API)
float result = lpf.Set(newSample).Resolve().Get();

// Or step-by-step
lpf.Set(newSample);
lpf.Resolve();
float result = lpf.Get();

// Continuous loop example
while (running) {
    float raw = readSensor();
    lpf.Set(raw).Resolve();
    float smoothed = lpf.Get();
    applyOutput(smoothed);
}
```

### Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `LowPassFilter& Set(Type data)` | `*this` | Loads a new input sample. |
| `LowPassFilter& Resolve()` | `*this` | Computes one filter iteration. |
| `Type Get()` | `Type` | Returns the current filtered output. |

## API Reference

### Hysteresis\<ValueType\>

| Member | Type | Description |
|--------|------|-------------|
| `Hysteresis()` | constructor | Default constructor. |
| `Hysteresis(ValueType halfGap, PullType pull)` | constructor | Initializes with half-gap and pull mode. |
| `Get(ValueType value)` | method | Processes input, returns filtered value. |
| `PullType` | enum class | `NoPull`, `PullUp`, `PullDown`. |

### KalmanFilter\<Type\>

| Member | Type | Description |
|--------|------|-------------|
| `KalmanFilter(Type, Type, Type, Type, Type)` | constructor | See constructor section above. |
| `Filter(Type measurement, Type controlInput)` | method | Predict-correct cycle. |
| `GetLastStateEstimate()` | method | Returns last estimate. |
| `SetMeasurementNoise(Type)` | method | Runtime noise tuning. |
| `SetProcessNoise(Type)` | method | Runtime noise tuning. |

### LowPassFilter\<Type\>

| Member | Type | Description |
|--------|------|-------------|
| `LowPassFilter()` | constructor | Default (frequencyTime=10, samplingTime=10). |
| `LowPassFilter(Type, Type)` | constructor | Custom frequency/sampling time. |
| `Set(Type)` | method | Load input sample. |
| `Resolve()` | method | Compute filter step. |
| `Get()` | method | Read filtered output. |
