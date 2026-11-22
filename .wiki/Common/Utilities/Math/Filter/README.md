# Filter

A collection of lightweight signal filtering utilities for embedded systems.

## Table of Contents

- [Quick Start](#quick-start)
- [Hysteresis](#hysteresis)
- [KalmanFilter](#kalmanfilter)
- [LowPassFilter](#lowpassfilter)
- [ComplementaryFilter](#complementaryfilter)
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

// Pitch and roll from an accelerometer and a gyroscope
ComplementaryFilter<> attitude;
attitude.Reset(firstSample);
attitude.Update(sample, dtSec);
float pitch = attitude.GetPitchDeg();

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

The first call to `Filter` sets the state estimate to `measurement / measurementMapping`, returns it, and ignores `controlInput`. Before the first call to `Filter`, `GetLastStateEstimate()` returns 0.

## LowPassFilter

`template <RealType Type = float>`

A first-order IIR (infinite impulse response) low-pass filter. Configured by a frequency time constant and a sampling time. Uses a fluent (chained) API.

### Constructor

```cpp
LowPassFilter(Type frequencyTime, Type samplingTime);
```

- `frequencyTime` -- inverse of the cutoff frequency (1 / f_cutoff).
- `samplingTime` -- the sampling period of the input signal.

The default constructor uses `frequencyTime = 10` and `samplingTime = 10`. It computes the coefficients in the same way, so the coefficient for the new sample and for the previous output are both 0.5.

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

`Resolve()` computes `output = kx0 * input + ky1 * output`. The output starts at `0`, so the first `Resolve()` after `Set()` returns `kx0 * sample`, not the sample itself. The output approaches the input over several samples.

## ComplementaryFilter

`ComplementaryFilter` (`Utilities/Math/Filter/ComplementaryFilter.h`) gives the pitch and roll of a body from one accelerometer
and one gyroscope. The gyroscope is integrated, and the gravity direction from the accelerometer corrects the drift slowly.
It is portable: it has no adapter, no RTOS and no UI notion, and the caller passes the time between samples. The inputs are
g for the accelerometer and deg/s for the gyroscope, with the gyroscope bias already removed by the caller.

### Number types

`template <RealType T = float> requires RangeAtLeast<T, 180.0>`. Write `ComplementaryFilter<>` for `float`, or
`ComplementaryFilter<IQ<Q>>`. The filter itself needs the angles, which span -180 to 180 degrees, so a type whose range is below
180 is rejected at compile time. The gyroscope rate is the largest input, and its full scale depends on the sensor and its
setting, so the caller picks a type that holds it:

| Type | Range | Accepted | Largest gyroscope rate and acceleration |
|------|-------|----------|-----------------------------------------|
| `float` | any | yes | no limit |
| `IQ<16>` | 32768 | yes | 32768 deg/s, 32768 g |
| `IQ<20>` | 2048 | yes | 2048 deg/s (a 2000 deg/s full scale), 2048 g |
| `IQ<22>` | 512 | yes | 512 deg/s (a 500 deg/s full scale), 512 g |
| `IQ<23>` | 256 | yes | 256 deg/s (a 250 deg/s full scale), 256 g |
| `IQ<24>` | 128 | no | rejected at compile time |

An input beyond the range saturates silently. A 3000 deg/s rate on `IQ<20>` gives a pitch of 190 degrees instead of 293, while
`float` gives 279. The caller must keep the inputs within the range. The `Sample` fields, the tuning and the `Update` time step
are all `T`.

Measured against a double reference over a 4 s scenario (host test `.tool/sim/TestComplementaryFilter.cpp`, bounds in the
test): float 0.00006 degree, `IQ<16>` 0.0015 degree, `IQ<20>` 0.0002 degree. The time step should be a value the type holds
exactly, such as 2^-10 s. At `IQ<16>` a 1 ms step is 66 raw steps, 0.7 percent high, and the gyroscope integral grows with it
(about 0.5 degree in 4 s), so use `IQ<20>` for a 1 kHz sample time.

### Angles

Pitch is the rotation about the sensor's Y axis, roll the rotation about X. Both are right-handed, so the gyroscope rate of an
axis is the derivative of its angle:

- accelerometer angles: `pitch = atan2(-ax, sqrt(ay^2 + az^2))`, `roll = atan2(ay, az)`
- the accelerometer angle is low-passed at `accelLowPassHz` (10 Hz by default): `alpha = 1 - exp(-2 pi f dt)`
- complementary filter with time constant `timeConstantSec` (1 s by default):
  `correction = trust * dt / (tau + dt)`, `angle = (angle + rate * dt) * (1 - correction) + accelLowPass * correction`
- `trust = clamp(1 - | |a| - 1 g | / accelTrustG, 0, 1)` with `accelTrustG` = 0.25 g by default. While the device accelerates,
  the accelerometer measures more than gravity and its angle is not used.

Consequence: a constant gyroscope bias `b` settles at an angle error of about `b * tau`. A bias of 0.5 deg/s gives 0.5 deg with
the default time constant. A host test checks the settled error with a time constant of 0.5 s
(`motion.complementary.gyro_bias_settles_at_bias_times_tau`).

A caller that sees a gap in the samples calls `Reset` instead of `Update`: the angles restart from gravity rather than
integrating across the gap. `TiltScroll` does this for gaps longer than its `maxGapSec`.

### Usage

```cpp
#include <Utilities/Math/Filter/ComplementaryFilter.h>

ComplementaryFilter<> filter;
ComplementaryFilter<>::Tuning tuning;
tuning.timeConstantSec = 1.0f;
filter.SetTuning(tuning);

ComplementaryFilter<>::Sample sample;
sample.accelX = ax;       // g
sample.accelY = ay;
sample.accelZ = az;
sample.gyroX = gx;        // deg/s, bias removed
sample.gyroY = gy;
sample.gyroZ = gz;

filter.Reset(firstSample);      // the first sample, and the first sample after a gap
filter.Update(sample, dtSec);   // every later sample; dtSec > 0
float pitch = filter.GetPitchDeg();
float roll = filter.GetRollDeg();
```

### Tuning

| Field | Default | Meaning |
|-------|---------|---------|
| `timeConstantSec` | 1.0 | How long the gyroscope is trusted before gravity takes over (s) |
| `accelLowPassHz` | 10.0 | Corner of the low-pass filter on the accelerometer angles (Hz) |
| `accelTrustG` | 0.25 | Width of the window around 1 g in which the gravity correction is trusted (g). Must be greater than zero |

### Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `SetTuning(const Tuning&)` | `void` | Sets the tuning. Used from the next `Update` on |
| `Reset(const Sample&)` | `void` | Sets both angles and the low-passed accelerometer angles to the gravity direction of the sample |
| `Update(const Sample&, dtSec)` | `void` | One filter step for a sample `dtSec` after the previous one |
| `GetPitchDeg()` / `GetRollDeg()` | `float` | The filtered angles in degrees |

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
