# FOC Math Blocks

Math blocks library for implementing Field-Oriented Control (FOC) in C++.
Rewritten from TI C28x DMC Library (IQmath macros) into modern C++ header-only style.

## Table of Contents

- [Quick Start](#quick-start)
- [Blocks from Simple to Complex](#blocks-from-simple-to-complex)
  - [1. Utilities (generators, ramps)](#1-utilities)
  - [2. Coordinate Transformations](#2-coordinate-transformations)
  - [3. PWM Generation](#3-pwm-generation)
  - [4. Speed and Voltage Measurement](#4-speed-and-voltage-measurement)
  - [5. Observers and Estimators](#5-observers-and-estimators)
  - [6. ACI Blocks (Induction Motor)](#6-aci-blocks)
  - [7. BLDC Commutation](#7-bldc-commutation)
- [Complete Examples](#complete-examples)
  - [FOC for PMSM](#foc-for-pmsm)
  - [V/Hz Open Loop](#vhz-open-loop)
  - [Sensorless FOC via SMO](#sensorless-foc-via-smo)

## Quick Start

All blocks are header-only. They use `VHAL.h` (types `uint8`, `int32`, etc.), the standard `<cmath>`, `<algorithm>`, `<limits>`, `<type_traits>` and `<concepts>`, and, for the blocks with a range requirement, `Utilities/Math/NumberRange.h` (which pulls in `IQ.h`).

Common API pattern:
```cpp
block.Set(...)     // Set inputs
     .Resolve();   // Compute
auto result = block.Get();  // Get result
```

### Number types and ranges

Every block that does arithmetic is a template on its number type `T`: `float` (the default) or `IQ<Q>` from `Utilities/Math/IQMath/IQ.h`. An `IQ<Q>` holds magnitudes below `2^(31 - Q)`: IQ16 up to 32768, IQ20 up to 2048, IQ22 up to 512 and IQ24 up to 128. The table gives, per block, the types it supports and the limit it has. The error figures are measured against the same block in double over 3000 steps of the reference motor (Rs 2, Rr 1.5, Ls = Lr 0.2, Lm 0.19, Ts 100 us, fb 50 Hz, per-unit values of about 0.3 to 0.9), so they show what each Q gives on that scenario.

| Block | Types | Range or Q limit |
|---|---|---|
| `Impulse` | none (integer period) | the period is a `uint32` count of calls, in the constructor and in `SetPeriod`; a float argument does not compile (`use of deleted function 'Impulse::Impulse(F) [with F = float]'`) |
| `CommutationTrigger<T>` | `float`, `IQ<Q>` for the voltages | the counters are integers; the decisions are signs, and IQ matches the double instance exactly. The nested types depend on `T`: write `CommutationTrigger<T>::Input`, not `CommutationTrigger::Input` (does not compile). `CommutationTrigger cmtn;` uses the float default |
| `ClarkeTransformation<T>` | `float`, `IQ<Q>` | max error against the formulas 1.7e-5 at Q16, 1.3e-6 at Q20 and 7e-8 at Q24 |
| `ParkTransformation<T>` | `float`, `IQ<Q>` | 2.3e-5 at Q16, 1.9e-6 at Q20 and 1.2e-7 at Q24. Sine and cosine take radians |
| `VoltageCalculator<T>` | `float`, `IQ<Q>` | 2e-5 at Q16, 9e-7 at Q20 and 9e-8 at Q24 |
| `SpaceVectorGenerator<T>` | `float`, `IQ<Q>` | 3e-5 at Q16, 2e-6 at Q20 and 1e-7 at Q24 (SVPWM output) |
| `SVGenMF<T>` | `float`, `IQ<Q>` | needs Q24 for 1e-4 (5.6e-5) and Q20 or higher in general: 1.8e-3 at Q20, 1.4e-2 at Q16. The step `freq * freqMax` is rounded each call and the bias adds up over the calls |
| `RampGenerator<T>` | `float`, `IQ<Q>` | matches the double instance on the same quantized step (error 0 at Q16 with step 2^-7, Q20 and Q24 with 0.01). The step product is rounded each call, so a step that is not a multiple of the LSB drifts |
| `RampController<T>` | `float`, `IQ<Q>` | the default resolution 2^-15 is 2^(Q - 15) LSB: 0 at Q13 and below, 2 LSB at Q16. Use a resolution of several LSB |
| `VHzProfile<T>` | `float`, `IQ<Q>` | no fixed range; within 1e-4 of the formulas at Q16 and Q24 (the slope is a quotient of the parameters) |
| `CurrentModel<T>` | `float`, `IQ<Q>` | `Kr = Ts / Tr` sets the Q: `Kr * 2^Q` LSBs (49 at Q16, 786 at Q20, 12583 at Q24 for Kr 7.5e-4). Setup values are not rounded to the Q (see below). Angle error 2.4e-2 turn at Q16, 7e-4 at Q20, 2e-5 at Q24 over 3000 steps |
| `ACIFluxEstimator<T>` | `float`, `IQ<Q>` | `requires` 2π in range, so Q 28 or lower (checked at compile time). The setup values are float for IQ, so a physical `Vb` (311 V) is held at IQ24. Only `Ib / Vb` enters the constants, so the per-sample currents and voltages are per-unit of `Ib` and `Vb`. Q20 keeps the angle within 1e-4 turn and the fluxes within 1e-4 |
| `ACISpeedEstimator<T>` | `float`, `IQ<Q>` | `K2 = 1 / (fb Ts)` must be below `2^(31 - Q)`: 200 (fb 50 Hz, Ts 100 us) fits up to Q23 (Q22 is what the tests run), so IQ24 needs fb above 78 Hz. The rpm is an int32 with a 64-bit product for IQ. `Ts` and the other setup values are float for IQ, not rounded to the Q, so the constants do not depend on the Q of `Ts`. The Q still bounds `K2` and the speed it reads back |
| `SlidingModeObserver<T>` | `float`, `IQ<Q>` | `requires` 2π in range, so Q 28 or lower. Only `Vb / Ib` enters, so the per-sample currents and voltages are per-unit of `Ib` and `Vb`. `1 - exp(-Rs Ts / Ls)` is computed as `-expm1` in float, so Q16 gives 1.4e-4 turn and Q24 2e-6 over 3000 steps |
| `SpeedCalculator<T>` | `float`, `IQ<Q>` | `K1 = 1 / (fb Ts)` must be below `2^(31 - Q)`: 200 fits up to Q23 (Q22 is what the tests run). `ComputeK1` does not check the range: a K1 at or above the limit saturates silently. Use `SpeedCalculator<T>::ComputeK1(fb, Ts)`, which takes `SetupReal<T>` values, so `Ts` is not rounded to the Q. The rpm is an int32 with a 64-bit product for IQ |
| `SpeedPeriodCalculator<T>` | `float`, `IQ<Q>` with Q 16 or lower | `requires` 32767 in range: the timer counts go up to 0x7FFF. IQ17 and above fail to compile. The speed is in `T`; the rpm is `float` for float and `int32` for IQ, from a 64-bit product, truncated toward zero. For IQ16 an rpm above 32767 is returned exactly (46800 rpm at 26 pu, from a 10-count period) |
| `Resolver`, `CounterTrigger`, `Ramp2Controller`, `Ramp3Controller` | integers only | not templates; no change |

**Range statements.** Each block header states its input range. The per-unit inputs must be below `2^(31 - Q)` in magnitude, and some blocks need less because an intermediate sum is larger than an input (Clarke below `2^(29 - Q)`, Park and SVPWM below `2^(30 - Q)`, VoltageCalculator modulation below `2^(29 - Q)`). The `RangeAtLeast` constraint is what the compiler checks. The other limits are documented in the headers.

**Setup values are float for IQ.** The setup inputs (`MotorParameters` for the constants blocks, and the `Ts` and `fb` of `ComputeK1`) are `SetupReal<T>`: `double` for `double`, and `float` for `float` and every `IQ<Q>`. They are read only when a constant is computed, so they are not rounded to the Q: `Ts = 100 us` is the float 100 us at any Q, and a physical `Vb` of 311 V fits IQ24. The constants are computed in `SetupReal<T>` and each is rounded to `T` once. Per-sample inputs stay `T`, so the Q still limits them. A constant from the setup carries the float error: about 1e-7 relative, and about 5e-7 where the formula subtracts close values (`Ls Lr - Lm^2` in `ACIFluxEstimator`). Measured against double, every constant of the reference motor is within one LSB up to Q24 (the worst is 0.98 LSB, `ACIFluxEstimator` K8 at Q24); a constant with `k * 2^Q` above about 2^21 can be a few LSB off, which is still below the speed resolution for the speed constants.

**Error figures.** The errors in this table are for the survey scenario of each block's host test (3000 steps, the motor values in the test). They are not bounds for other motors or step counts.

**Choosing Q.** A constant `k` is rounded to half an LSB, so its relative error is `0.5 / (k * 2^Q)`. Choose the Q from the smallest constant the block uses (for the estimators it is `Kr` or `1 / (fb Ts)` in the table) so that `k * 2^Q` is at least about 1000. The integrating blocks (the flux, angle and speed estimators, SVGenMF) add a rounding bias on every call, so a long run needs a larger Q than a single call does. The ranges in the table are checked at compile time for `SpeedPeriodCalculator`; the other limits depend on the motor and the sampling period, so they are up to the user.

```cpp
#include <Utilities/Math/IQMath/IQMath.h>
#include <Utilities/Math/FOC/SpeedPeriodCalculator.h>

SpeedPeriodCalculator<IQ<16>> speedPr;   // timer counts fit in IQ16
// SpeedPeriodCalculator<IQ<24>> speedPr;
//   error: template constraint failure for 'template<class T> requires RangeAtLeast<T, 3.2767e+4> class SpeedPeriodCalculator'
//   note: the expression '(NumberRange<T>::maxAbs) >= Need [with T = IQ<24, 256>; Need = 3.2767e+4]' evaluated to 'false'
```


---

## Blocks from Simple to Complex

### 1. Utilities

#### Impulse — periodic impulse

```cpp
#include <Utilities/Math/FOC/Impulse.h>

Impulse impulse(1000);  // Period = 1000 calls, a uint32 count; impulse.SetPeriod(500) takes a uint32 too

// In timer interrupt:
impulse.Resolve();
if (impulse.IsImpulse()) {
    // Perform action every 1000 ticks
}
```

#### RampController — smooth ramp to setpoint

```cpp
#include <Utilities/Math/FOC/RampController.h>

RampController<float> ramp(
    5,       // delayMax — delay between steps (in Resolve calls)
    -1.0f,   // amplitudeMin
    1.0f,    // amplitudeMax
    0.001f   // resolution — step size
);

ramp.SetTargetPosition(0.8f);

// In control loop:
ramp.Resolve();
float smoothRef = ramp.Get().position;   // Smoothly ramps up to 0.8
bool reached    = ramp.Get().equalFlag;  // true when target is reached
```

#### RampGenerator — sawtooth angle generator

```cpp
#include <Utilities/Math/FOC/RampGenerator.h>

RampGenerator<float> rampGen;
rampGen.stepAngleMax = 0.001f;  // Maximum angle step per call
rampGen.outputMin = 0.0f;
rampGen.outputMax = 1.0f;

rampGen.Set(
    1.0f,   // frequency (pu) — 1.0 = base frequency
    1.0f,   // gain
    0.0f    // offset
);

// In loop:
rampGen.Resolve();
float angle = rampGen.Get();  // 0.0 → 1.0, then reset
```

#### CounterTrigger — sector counter (modulo 6)

```cpp
#include <Utilities/Math/FOC/CounterTrigger.h>

CounterTrigger counter(CounterTrigger::Direction::Forward);

// Upon receiving commutation signal:
counter.SetTrigger(true);
counter.Resolve();
int sector = counter.Get().step;  // 0, 1, 2, 3, 4, 5, 0, ...
```


### 2. Coordinate Transformations

#### ClarkeTransformation — abc → αβ

```cpp
#include <Utilities/Math/FOC/ClarkeTransformation.h>

ClarkeTransformation<float> clarke;

// Option 1: from two currents (third = -(Ia + Ib))
clarke.SetPhase({Ia, Ib, 0});
clarke.Resolve(ClarkeTransformation<float>::CurrentsMode::C2);

// Option 2: from three currents
clarke.SetPhase({Ia, Ib, Ic});
clarke.Resolve(ClarkeTransformation<float>::CurrentsMode::C3);

float iAlpha = clarke.GetClarke().alpha;
float iBeta  = clarke.GetClarke().beta;

// Inverse transform (αβ → abc):
clarke.SetClarke({vAlpha, vBeta});
clarke.ResolveInverse();
float Va = clarke.GetPhase().a;
float Vb = clarke.GetPhase().b;
float Vc = clarke.GetPhase().c;
```

#### ParkTransformation — αβ → dq

```cpp
#include <Utilities/Math/FOC/ParkTransformation.h>

ParkTransformation<float> park;

// Forward: αβ → dq (requires electrical angle in radians)
park.SetAxis({iAlpha, iBeta, electricalAngle});
park.Resolve();
float Id = park.GetPark().dAxis;
float Iq = park.GetPark().qAxis;

// Inverse: dq → αβ
park.SetPark({Vd, Vq, electricalAngle});
park.ResolveInverse();
float vAlpha = park.GetAxis().alpha;
float vBeta  = park.GetAxis().beta;
```


### 3. PWM Generation

#### SpaceVectorGenerator — SVPWM (3 modes)

```cpp
#include <Utilities/Math/FOC/SpaceVectorGenerator.h>

SpaceVectorGenerator<float> svg;

// Inputs: normalized αβ voltages (relative to Vdc/2)
svg.Set(vAlpha_norm, vBeta_norm);

// Mode 1: Standard sector SVPWM
svg.Resolve();

// Mode 2: Common-mode SVPWM (centered, minimal harmonics)
svg.ResolveCommonMode();

// Mode 3: Discontinuous PWM (DPWM, one phase always clamped)
svg.ResolveDiscontinuousPwmMode();

auto pwm = svg.Get();
// pwm.phaseA, pwm.phaseB, pwm.phaseC — duty cycles for PWM
```

#### SVGenMF — medium-frequency SVPWM

```cpp
#include <Utilities/Math/FOC/SVGenMF.h>

SVGenMF<float> svmf;
svmf.freqMax = 6.0f * baseFreq * Ts;  // Maximum angle step

svmf.Set(
    1.0f,   // frequency (pu)
    0.9f,   // gain (modulation depth)
    0.0f    // offset
);

// In PWM loop:
svmf.Resolve();
auto pwm = svmf.Get();
SetPWM(pwm.phaseA, pwm.phaseB, pwm.phaseC);
```

#### VoltageCalculator — modulation → phase voltages

```cpp
#include <Utilities/Math/FOC/VoltageCalculator.h>

VoltageCalculator<float> voltCalc;
voltCalc.outputOfPhase = true;  // true if PWM is inverted

voltCalc.Set(
    dcBusVoltage,      // DC bus voltage
    modulationA,       // Phase A duty cycle (from SVGEN)
    modulationB,       // Phase B duty cycle
    modulationC        // Phase C duty cycle
);
voltCalc.Resolve();

auto v = voltCalc.Get();
// v.phaseA, v.phaseB, v.phaseC — phase voltages
// v.alpha, v.beta — αβ voltages (for observers)
```


### 4. Speed and Voltage Measurement

#### SpeedCalculator — speed via angle differentiation

```cpp
#include <Utilities/Math/FOC/SpeedCalculator.h>

SpeedCalculator<float> speedCalc;
speedCalc.baseSpeedRpm = 3600;  // Base speed for normalization

// Differentiator and LPF coefficients
// K1 = 1 / (fb * Ts), where fb — base frequency, Ts — sampling period.
// ComputeK1 works in SetupReal<T> and rounds once; for IQ<Q> K1 must be below 2^(31 - Q)
speedCalc.K1 = SpeedCalculator<float>::ComputeK1(60.0, 0.001);  // fb=60Hz, Ts=1ms
// K2, K3 — LPF coefficients: K2 + K3 = 1
speedCalc.K2 = 0.95f;   // Previous value weight (higher = stronger filtering)
speedCalc.K3 = 0.05f;   // New value weight

// In loop (angle normalized 0..1):
speedCalc.Set(electricalAngleNorm).Resolve();
float speedPU  = speedCalc.Get().speed;      // Speed in pu (-1..1)
int   speedRPM = speedCalc.Get().speedRpm;   // Speed in RPM
```

#### SpeedPeriodCalculator — speed via period

```cpp
#include <Utilities/Math/FOC/SpeedPeriodCalculator.h>

// Two modes: by event period or by timestamp
SpeedPeriodCalculator<float> speedPr(
    100.0f,  // speedScaler — scaler converting 1/N cycles to speed
    1800.0f, // baseRpm — scaler converting per-unit speed to rpm
    0x7FFF,  // maximumOutput — timer count range (0x7FFF); T must hold 32767, so IQ<Q> needs Q 16 or lower
    SpeedPeriodCalculator<float>::InputSelect::EventPeriod
);

// On event (Hall/encoder):
speedPr.SetEventPeriod(measuredPeriod);
speedPr.Resolve();
float speedPU  = speedPr.Get().speed;
float speedRPM = speedPr.Get().speedRpm;
```


### 5. Observers and Estimators

#### SlidingModeObserver — sensorless PMSM

The base values below are physical (volts and amps). The setup values are float for `IQ<Q>` too, so the same physical values work with any Q. Only `Vb / Ib` enters the constants, so the per-sample currents and voltages must be per-unit of `Ib` and `Vb`.

```cpp
#include <Utilities/Math/FOC/SlidingModeObserver.h>

// Step 1: Compute constants from motor parameters
auto smoConst = SlidingModeObserver<float>::ComputeConstants({
    .Rs = 0.4f,     // Stator resistance [Ohm]
    .Ls = 0.0006f,  // Stator inductance [H]
    .Ib = 3.5f,     // Base current [A]
    .Vb = 14.0f,    // Base voltage [V] (Vdc / sqrt(3))
    .Ts = 50e-6f    // Sampling period [s]
});

// Step 2: Configure observer
SlidingModeObserver<float> smo;
smo.constants = smoConst;
smo.Kslide = 0.1f;    // Sliding mode gain
smo.Kslf   = 0.05f;   // Back-EMF filter constant
smo.E0     = 0.5f;    // Saturation boundary

// Step 3: In control loop (inputs normalized to base values)
smo.Set(vAlpha_pu, vBeta_pu, iAlpha_pu, iBeta_pu);
smo.Resolve();

float rotorAngle = smo.Get().theta;  // Estimated rotor angle (0..1)
```

#### CurrentModel — rotor current model (for ACI)

```cpp
#include <Utilities/Math/FOC/CurrentModel.h>

auto cmConst = CurrentModel<float>::ComputeConstants({
    .Rr = 2.0f,     // Rotor resistance [Ohm]
    .Lr = 0.1f,     // Rotor inductance [H]
    .fb = 60.0f,    // Base electrical frequency [Hz]
    .Ts = 0.001f    // Sampling period [s]
});

CurrentModel<float> curModel;
curModel.constants = cmConst;

// In loop (all inputs in pu):
curModel.Set(
    Id,    // d-axis stator current
    Iq,    // q-axis stator current
    Wr     // Electrical angular speed of rotor
).Resolve();

float thetaFlux = curModel.Get().theta;  // Rotor flux angle (0..1)
```

#### VHzProfile — V/Hz profile

```cpp
#include <Utilities/Math/FOC/VHzProfile.h>

VHzProfile<float> vhz;
vhz.parameters = {
    .lowFreq  = 10.0f,   // Lower frequency boundary [pu]
    .highFreq = 50.0f,   // Frequency at rated voltage [pu]
    .voltMax  = 1.0f,    // Voltage at highFreq [pu]
    .voltMin  = 0.1f     // Voltage at lowFreq [pu]
};

// In loop:
vhz.Set(currentFrequency).Resolve();
float voltage = vhz.Get().voltage;
// Below lowFreq → voltMin (boost)
// lowFreq..highFreq → linear interpolation
// Above highFreq → voltMax (constant). Resolve() reads only the four fields above.
```

#### Resolver — resolver → angle

```cpp
#include <Utilities/Math/FOC/Resolver.h>

Resolver resolver;
resolver.parameters = {
    .stepsPerTurn = 4096,   // Encoder resolution
    .mechScaler   = 16384,  // 0.9999/totalCount in Q30
    .polePairs    = 4,      // Number of pole pairs
    .initTheta    = 0       // Offset between index and phase A
};

// When reading position:
resolver.Set(rawEncoderValue).Resolve();
int32 elecAngle = resolver.Get().elecTheta;  // Electrical angle (Q24)
int32 mechAngle = resolver.Get().mechTheta;  // Mechanical angle (Q24)
```


### 6. ACI Blocks

#### ACIFluxEstimator — flux estimation

The base values below are physical (amps and volts). The setup values are float for `IQ<Q>` too, so a physical `Vb` of 200 V works with any Q. Only `Ib / Vb` enters the constants, so the per-sample currents and voltages must be per-unit of `Ib` and `Vb`.

```cpp
#include <Utilities/Math/FOC/ACIFluxEstimator.h>

auto feConst = ACIFluxEstimator<float>::ComputeConstants({
    .Rs = 2.0f,     // Stator resistance [Ohm]
    .Rr = 3.0f,     // Rotor resistance [Ohm]
    .Ls = 0.1f,     // Stator inductance [H]
    .Lr = 0.12f,    // Rotor inductance [H]
    .Lm = 0.09f,    // Magnetizing inductance [H]
    .Ib = 10.0f,    // Base current [A]
    .Vb = 200.0f,   // Base voltage [V]
    .Ts = 0.001f    // Sampling period [s]
});

ACIFluxEstimator<float> fluxEst;
fluxEst.constants = feConst;
fluxEst.piGains = { .Kp = 0.01f, .Ki = 0.001f };

// In loop (αβ currents and voltages in pu):
fluxEst.Set(idsS, iqsS, udsS, uqsS).Resolve();

auto flux = fluxEst.Get();
float thetaFlux = flux.thetaFlux;  // Rotor flux angle (0..1)
float psiDr     = flux.psiDrS;     // Rotor d-flux (pu)
float psiQr     = flux.psiQrS;     // Rotor q-flux (pu)
```

#### ACISpeedEstimator — speed estimation

```cpp
#include <Utilities/Math/FOC/ACISpeedEstimator.h>

auto seConst = ACISpeedEstimator<float>::ComputeConstants({
    .Rr = 3.0f,     // Rotor resistance [Ohm]
    .Lr = 0.12f,    // Rotor inductance [H]
    .fb = 60.0f,    // Base frequency [Hz]
    .fc = 5.0f,     // LPF cutoff frequency [Hz]
    .Ts = 0.001f    // Sampling period [s]
});

ACISpeedEstimator<float> speedEst;
speedEst.constants = seConst;
speedEst.baseRpm = 1800;

// In loop (inputs from ACIFluxEstimator):
speedEst.Set(iqsS, idsS, psiDrS, psiQrS, thetaFlux).Resolve();

float speedPU  = speedEst.Get().speed;      // Speed in pu
int   speedRPM = speedEst.Get().speedRpm;   // Speed in RPM
```


### 7. BLDC Commutation

#### CommutationTrigger — commutation by BEMF

```cpp
#include <Utilities/Math/FOC/CommutationTrigger.h>

CommutationTrigger<float> cmtn;   // the voltages are T; CommutationTrigger<IQ<20>> takes IQ voltages

cmtn.SetWindowsNoise({
    .delta = 2,
    .dynamicThreshold = 10
});

cmtn.SetOptions({
    .maxOutput = 0x0000FFFF
});

// In ADC interrupt (after measuring phase voltages):
cmtn.SetInput({
    .Va = phaseA_voltage,
    .Vb = phaseB_voltage,
    .Vc = phaseC_voltage,
    .commutationPointer = currentSector,   // 0..5
    .virtualTimer = timerCount
});
cmtn.Resolve();

if (cmtn.Get().trigger) {
    // Switch to next commutation
    counter.SetTrigger(true);
    counter.Resolve();
    ApplyCommutation(counter.Get().step);
}
```


---

## Complete Examples


### Where Input Data Comes From

#### Phase Currents (Ia, Ib, Ic)

Measured by ADC through shunt resistors (inline shunt) or current sensors (ACS712, hall).

```cpp
// Example: 12-bit ADC, Vref=3.3V, 10mOhm shunt, x20 amplifier, 1.65V offset
float AdcToAmps(uint16 adcRaw) {
    float voltage = (adcRaw / 4095.0f) * 3.3f;  // ADC → voltage
    float diff = voltage - 1.65f;                 // Remove offset (bipolar)
    return diff / (0.01f * 20.0f);                // Voltage → current [A]
    // 0.01 = shunt [Ohm], 20 = gain
}
```

If only 2 phases are measured (shunts on low-side switches):
```cpp
float Ia = AdcToAmps(adcA);
float Ib = AdcToAmps(adcB);
float Ic = -(Ia + Ib);  // Third current from KCL: Ia + Ib + Ic = 0
```

#### Electrical angle from encoder

```cpp
// Incremental encoder (connected to timer in encoder mode)
// encoderCount — current timer counter value
// cpr — counts per revolution (for 1000 PPR encoder: 4000 in x4 mode)
// polePairs — number of motor pole pairs
// offsetCount — offset from calibration (aligning index mark with phase A)

float GetElectricalAngle(uint32 encoderCount, uint32 cpr, uint32 polePairs, int32 offsetCount) {
    int32 adjusted = (int32)(encoderCount - offsetCount);

    // Mechanical angle in radians
    float mechAngle = ((float)(adjusted % (int32)cpr) / (float)cpr) * 2.0f * M_PI;

    // Electrical = mechanical × polePairs
    float elecAngle = mechAngle * polePairs;

    // Normalize to [0, 2π)
    elecAngle = fmodf(elecAngle, 2.0f * M_PI);
    if (elecAngle < 0) elecAngle += 2.0f * M_PI;

    return elecAngle;  // [rad] — directly into ParkTransformation
}
```

```cpp
// Absolute magnetic encoder (AS5048A, MA730, etc.) — SPI/I2C
// Returns 0..16383 (14-bit) per revolution

float GetElectricalAngleFromAbsEncoder(uint16 rawAngle, uint16 polePairs, uint16 offsetRaw) {
    uint16 adjusted = rawAngle - offsetRaw;
    float mechAngle = ((float)adjusted / 16384.0f) * 2.0f * M_PI;
    float elecAngle = fmodf(mechAngle * polePairs, 2.0f * M_PI);
    if (elecAngle < 0) elecAngle += 2.0f * M_PI;
    return elecAngle;  // [rad]
}
```

#### Encoder Offset Calibration

On first startup you need to find `offsetCount` / `offsetRaw` — align the electrical zero
with phase A:

```cpp
void CalibrateEncoder() {
    // 1. Apply voltage vector at angle 0 (phase A)
    //    This is Vd=amplitude, Vq=0, theta=0
    ParkTransformation<float> invPark;
    invPark.SetPark({5.0f, 0.0f, 0.0f});  // Vd=5V, Vq=0, angle=0
    invPark.ResolveInverse();
    auto ab = invPark.GetAxis();
    // ... write ab.alpha, ab.beta to PWM ...

    // 2. Wait 2-3 seconds for the rotor to align

    // 3. Read current encoder position — this is the offset
    offsetCount = ReadEncoder();  // For incremental
    offsetRaw   = ReadSPI();      // For absolute
}
```

#### DC Bus Voltage (Vdc)

```cpp
// Voltage divider + ADC. Example: 24V bus, 10:1 divider, Vref=3.3V
float GetVdc(uint16 adcRaw) {
    return (adcRaw / 4095.0f) * 3.3f * 10.0f;  // [V]
}
```


---

### FOC for PMSM

Full FOC loop with position sensor (encoder/resolver).

```
               ┌──────────┐     ┌──────────┐     ┌──────┐
  Ia,Ib,Ic ───>│  Clarke  ├────>│   Park   ├────>│ PI_d │──> Vd
               │ (abc→αβ) │     │  (αβ→dq) │     └──────┘
               └──────────┘     └────┬─────┘     ┌──────┐
                    ↑                │      ────>│ PI_q │──> Vq
                    │                │           └──────┘
                    │        electricalAngle        │
                    │                │              │
               ┌──────────┐     ┌────┴─────┐     ┌──────────┐
  PWM A,B,C <──│  SVGEN   │<────│ Inv Park │<────┤ Vd, Vq   │
               │ (αβ→PWM) │     │ (dq→αβ)  │     └──────────┘
               └──────────┘     └──────────┘
```

#### PWM and Interrupt Setup

FOC code runs in an **ADC interrupt**, which is triggered by the **same PWM timer**
that generates PWM for the motor power switches.

```
PWM timer (TIM1/TIM8 on STM32, EPWM on C2000)
    │
    ├─── Channels 1,2,3 → half-bridges for phases A, B, C (complementary + deadtime)
    │
    └─── Trigger Output → ADC Start
              │
              └─── ADC EOC IRQ → FOC_ISR()
```

**Why this approach:**
- The PWM timer generates a trigger at the **center of the PWM period** (in center-aligned mode).
  At this moment all low-side switches are on, and current through shunts is stable — no noise from switching.
- Using a **separate timer** is possible, but it must be synchronized with the PWM.
  Otherwise the ADC will sample at a random point within the PWM period, and currents will be noisy.

**Choosing PWM frequency (= FOC call frequency):**
- 10-20 kHz — typical for most BLDC/PMSM
- Higher = better dynamics, but more switching losses and MCU load
- Below 8 kHz — audible motor whine

If the MCU cannot keep up, you can divide: PWM at 20 kHz, FOC on every 2nd or 4th call.

```cpp
#include <Utilities/Math/FOC/ClarkeTransformation.h>
#include <Utilities/Math/FOC/ParkTransformation.h>
#include <Utilities/Math/FOC/SpaceVectorGenerator.h>
#include <Utilities/Math/PID/PidController.h>

// ===== Motor parameters =====
const float Vdc = 24.0f;          // DC bus voltage [V] (or measure via ADC)
const uint32 POLE_PAIRS = 4;
const uint32 PWM_FREQ = 20000;    // 20 kHz

// ===== Global objects (persist between ISR calls) =====
ClarkeTransformation<float> clarke;
ParkTransformation<float> park;
ParkTransformation<float> invPark;
SpaceVectorGenerator<float> svg;

// PI controller for d-axis current: keeps Id = 0 (for SPM motors)
PidController<float> pidId({
    .proportional = 5.0f,    // Kp — tune: start with Ls * bandwidth
    .integral = 500.0f,      // Ki — tune: start with Rs * bandwidth
    .derivative = 0.0f       // D not used in current loop
}, {
    .max = Vdc / 2.0f,       // Output limit [V] — no more than half-bus
    .min = -Vdc / 2.0f
});

// PI controller for q-axis current: controls torque
PidController<float> pidIq({
    .proportional = 5.0f,
    .integral = 500.0f,
    .derivative = 0.0f
}, {
    .max = Vdc / 2.0f,
    .min = -Vdc / 2.0f
});

// PI speed controller (outer loop, can be called less frequently)
PidController<float> pidSpeed({
    .proportional = 0.5f,
    .integral = 10.0f,
    .derivative = 0.0f
}, {
    .max = 3.5f,              // Limit = rated current [A]
    .min = -3.5f
});

float IqRef = 0;  // q-axis current reference [A] — from speed loop

// ===== ADC interrupt — called at PWM frequency =====
//
// This is the PWM timer interrupt (TIM1 on STM32) that drives the power switches.
// NOT an arbitrary timer — specifically the one that generates PWM for phases A, B, C.
//
// Currents Ia, Ib, Ic — from ADC through shunt resistors:
//   Ia = (ADC_raw - 2048) * (Vref / 4096) / (R_shunt * amplifier_gain) [A]
//
// electricalAngle — from encoder:
//   elecAngle = (encoderCount / CPR) * 2π * polePairs [rad]
//
void FOC_ISR() {
    // --- Get input data ---
    // Phase currents: ADC → amperes (see AdcToAmps above)
    float Ia = AdcToAmps(ADC_ReadPhaseA());
    float Ib = AdcToAmps(ADC_ReadPhaseB());
    float Ic = -(Ia + Ib);  // If only 2 shunts

    // Electrical angle: encoder → radians
    float electricalAngle = GetElectricalAngle(
        TIM_GetEncoderCount(), encoderCPR, POLE_PAIRS, encoderOffset
    );

    // --- 1. Measurement: abc → αβ → dq ---
    clarke.SetPhase({Ia, Ib, Ic});
    clarke.Resolve(ClarkeTransformation<float>::CurrentsMode::C3);
    auto ab = clarke.GetClarke();

    park.SetAxis({ab.alpha, ab.beta, electricalAngle});
    park.Resolve();
    auto dq = park.GetPark();
    // dq.dAxis — magnetizing current (want = 0 for SPM)
    // dq.qAxis — torque current (controlled via speed PI)

    // --- 2. Current PI controllers ---
    // IdRef = 0 for surface permanent magnets (SPM). For IPM can be < 0 (field weakening)
    // IqRef is set from the outer speed/position loop (or directly for torque control)
    //
    // reference = current setpoint [A]
    // feedback  = measured current [A] (from Park)
    // frequency = Resolve call frequency [Hz] (= PWM frequency)
    float Vd = pidId.SetInpit({
        .feedback  = dq.dAxis,
        .reference = 0.0f,       // Id = 0 for SPM
        .frequency = PWM_FREQ
    }).Resolve().Get();           // Output [V]

    float Vq = pidIq.SetInpit({
        .feedback  = dq.qAxis,
        .reference = IqRef,      // From speed loop [A]
        .frequency = PWM_FREQ
    }).Resolve().Get();           // Output [V]

    // --- 3. Actuation: dq → αβ → PWM duty cycles ---
    invPark.SetPark({Vd, Vq, electricalAngle});
    invPark.ResolveInverse();
    auto vab = invPark.GetAxis();
    // vab.alpha, vab.beta — voltages in stationary frame [V]

    // Normalize to half-bus: SVGEN expects input in approximately [-1, 1] range
    svg.Set(vab.alpha / (Vdc / 2.0f), vab.beta / (Vdc / 2.0f));
    svg.ResolveCommonMode();
    auto pwm = svg.Get();
    // pwm.phaseA/B/C — in range [-1, 1]

    // Set PWM duty cycle: convert [-1, 1] → [0, 1]
    // 0.0 = low-side switch always on, 1.0 = high-side switch always on
    // These are values for PWM timer compare register:
    //   TIM1->CCR1 = duty * TIM1->ARR
    TIM_SetDutyA((pwm.phaseA + 1.0f) * 0.5f);
    TIM_SetDutyB((pwm.phaseB + 1.0f) * 0.5f);
    TIM_SetDutyC((pwm.phaseC + 1.0f) * 0.5f);
}

// ===== Speed loop (can be called less frequently — e.g., every 10th FOC call) =====
void SpeedLoop_Update(float targetSpeedRPM, float measuredSpeedRPM) {
    IqRef = pidSpeed.SetInpit({
        .feedback  = measuredSpeedRPM,
        .reference = targetSpeedRPM,
        .frequency = PWM_FREQ / 10   // If called every 10th time
    }).Resolve().Get();              // Output = IqRef [A]
}
```


### V/Hz Open Loop

Simplest control — for pumps, fans, initial acceleration before switching
to sensorless FOC. **Does not require current sensors or encoder.**

Principle: voltage is proportional to frequency, angle is generated programmatically.

#### Choosing Interrupt Frequency

V/Hz is not tied to a specific PWM for current measurement (currents are not measured).
It can be called from the interrupt of the **same PWM timer** that drives the power switches.
Frequency = PWM frequency (10-20 kHz), or a divider of it.

```cpp
#include <Utilities/Math/FOC/VHzProfile.h>
#include <Utilities/Math/FOC/RampGenerator.h>
#include <Utilities/Math/FOC/SpaceVectorGenerator.h>
#include <Utilities/Math/FOC/ParkTransformation.h>

// ===== Parameters =====
const float Vdc = 300.0f;          // DC bus voltage [V]
const float PWM_FREQ = 10000.0f;   // 10 kHz
const float Ts = 1.0f / PWM_FREQ;  // 100 us

// ===== Objects =====
RampGenerator<float> rampGen;
VHzProfile<float> vhz;
SpaceVectorGenerator<float> svg;
ParkTransformation<float> invPark;

void VHz_Init() {
    // stepAngleMax determines the maximum angle slew rate
    // For base frequency fbase and PWM frequency fpwm:
    //   stepAngleMax = fbase / fpwm
    // Example: fbase=60Hz, fpwm=10kHz → stepAngleMax = 60/10000 = 0.006
    rampGen.stepAngleMax = 0.006f;
    rampGen.outputMin = 0.0f;
    rampGen.outputMax = 1.0f;

    // V/Hz profile: below lowFreq — boost, lowFreq..highFreq — linear, above — const
    vhz.parameters = {
        .lowFreq  = 0.15f,   // 15% of base (boost region)
        .highFreq = 0.85f,   // 85% of base (rated voltage)
        .voltMax  = 1.0f,    // Rated voltage (pu)
        .voltMin  = 0.1f     // Boost voltage at low frequency (pu)
    };
}

// ===== PWM timer interrupt (same timer that drives the switches) =====
// freqRef — frequency reference in pu (0..1), where 1.0 = base frequency
// Set from main loop (button, potentiometer, UART, etc.)
void VHz_ISR() {
    // Angle generation — rampGen increments angle on each call
    // frequency=freqRef → at freqRef=1.0 angle increases at maximum rate
    rampGen.Set(freqRef, 1.0f, 0.0f);
    rampGen.Resolve();
    float angle = rampGen.Get() * 2.0f * M_PI;  // 0..1 → 0..2π [rad]

    // Voltage amplitude from V/Hz profile
    vhz.Set(freqRef).Resolve();
    float Vamp = vhz.Get().voltage;  // 0..1 [pu]

    // Voltage formation: Vd=0, Vq=Vamp
    // (voltage vector perpendicular to "imaginary" flux vector)
    invPark.SetPark({0.0f, Vamp, angle});
    invPark.ResolveInverse();
    auto vab = invPark.GetAxis();

    svg.Set(vab.alpha, vab.beta);
    svg.ResolveCommonMode();
    auto pwm = svg.Get();

    // Duty cycles to PWM: [-1,1] → [0,1]
    TIM_SetDutyA((pwm.phaseA + 1.0f) * 0.5f);
    TIM_SetDutyB((pwm.phaseB + 1.0f) * 0.5f);
    TIM_SetDutyC((pwm.phaseC + 1.0f) * 0.5f);
}
```


### Sensorless FOC via SMO

FOC without position sensor — rotor angle is estimated from back-EMF
via Sliding Mode Observer. **Requires current sensors, does not require encoder.**

> Back-EMF is proportional to speed, so SMO **does not work at 0 RPM**.
> For startup use V/Hz ramp-up to ~5-10% of rated speed, then switch to SMO.

```cpp
#include <Utilities/Math/FOC/ClarkeTransformation.h>
#include <Utilities/Math/FOC/ParkTransformation.h>
#include <Utilities/Math/FOC/SpaceVectorGenerator.h>
#include <Utilities/Math/FOC/SlidingModeObserver.h>
#include <Utilities/Math/FOC/VoltageCalculator.h>
#include <Utilities/Math/FOC/SpeedCalculator.h>
#include <Utilities/Math/PID/PidController.h>

// ===== Motor parameters (from datasheet or measurements) =====
const float Rs = 0.4f;       // Stator resistance [Ohm] (measure with multimeter between phases / 2)
const float Ls = 0.6e-3f;    // Stator inductance [H] (measure with LCR meter between phases / 2)
const float Irated = 3.5f;   // Rated current [A] (from datasheet)
const float Vdc = 24.0f;     // DC bus voltage [V]
const uint32 PWM_FREQ = 20000;
const float Ts = 1.0f / PWM_FREQ;

// Base values for normalization (per-unit system)
const float Ibase = Irated;
const float Vbase = Vdc / sqrtf(3.0f);   // Line-to-line → phase

// ===== Objects =====
SlidingModeObserver<float> smo;
SpeedCalculator<float> speedCalc;
ClarkeTransformation<float> clarke;
ParkTransformation<float> park, invPark;
VoltageCalculator<float> voltCalc;
SpaceVectorGenerator<float> svg;

// Current PI controllers (same as FOC for PMSM example)
PidController<float> pidId({.proportional = 5.0f, .integral = 500.0f}, {.max = Vdc/2, .min = -Vdc/2});
PidController<float> pidIq({.proportional = 5.0f, .integral = 500.0f}, {.max = Vdc/2, .min = -Vdc/2});

float IqRef = 0;  // From speed loop [A]

// Previous step duty cycles — needed by VoltageCalculator to reconstruct voltages
float lastDutyA = 0.5f, lastDutyB = 0.5f, lastDutyC = 0.5f;

void SensorlessFOC_Init() {
    // SMO: constants computed from motor parameters
    smo.constants = SlidingModeObserver<float>::ComputeConstants({
        .Rs = Rs,
        .Ls = Ls,
        .Ib = Ibase,
        .Vb = Vbase,
        .Ts = Ts
    });
    smo.Kslide = 0.1f;    // Start with 0.05-0.2, increase if angle is noisy
    smo.Kslf   = 0.05f;   // Back-EMF filter: smaller = stronger filtering, more delay

    // SpeedCalculator
    // K1 = differentiator coefficient: 1 / (baseFreq * Ts)
    //   baseFreq = (ratedRPM / 60) * polePairs
    //   Example: 3600 RPM, 4pp → baseFreq = 240 Hz → K1 = 1/(240*50e-6) = 83.3
    speedCalc.K1 = 83.3f;
    speedCalc.K2 = 0.95f;  // LPF: higher = stronger smoothing (0.9-0.99)
    speedCalc.K3 = 0.05f;  // K2 + K3 = 1.0
    speedCalc.baseSpeedRpm = 3600;
}

// ===== ADC interrupt (triggered by motor PWM timer) =====
//
// Same PWM timer that drives the power switches.
// No arguments passed — everything is read from ADC and previous duty cycles inside ISR.
//
void SensorlessFOC_ISR() {
    // --- Input data ---

    // Phase currents: ADC → amperes
    float Ia = AdcToAmps(ADC_ReadPhaseA());
    float Ib = AdcToAmps(ADC_ReadPhaseB());
    float Ic = -(Ia + Ib);

    // DC bus voltage (can measure each time or use a constant)
    float vdc = GetVdc(ADC_ReadVbus());

    // --- 1. Reconstruct αβ voltages from previous step duty cycles ---
    // VoltageCalculator computes phase voltages from modulation
    voltCalc.Set(vdc, lastDutyA, lastDutyB, lastDutyC);
    voltCalc.Resolve();
    auto volt = voltCalc.Get();

    // --- 2. Clarke: phase currents → αβ ---
    clarke.SetPhase({Ia, Ib, Ic});
    clarke.Resolve(ClarkeTransformation<float>::CurrentsMode::C3);
    auto ab = clarke.GetClarke();

    // --- 3. SMO: rotor angle estimation from back-EMF ---
    // Inputs are normalized to base values (per-unit)
    smo.Set(
        volt.alpha / Vbase,    // αβ voltage [pu]
        volt.beta  / Vbase,
        ab.alpha   / Ibase,    // αβ current [pu]
        ab.beta    / Ibase
    ).Resolve();

    float thetaNorm = smo.Get().theta;                  // Angle [0..1]
    float estAngle  = thetaNorm * 2.0f * M_PI;          // Angle [rad] — for Park

    // --- 4. Speed estimation ---
    speedCalc.Set(thetaNorm).Resolve();
    // speedCalc.Get().speedRpm — estimated speed [RPM]

    // --- 5. Park: αβ currents → dq (with estimated angle) ---
    park.SetAxis({ab.alpha, ab.beta, estAngle});
    park.Resolve();
    auto dq = park.GetPark();

    // --- 6. Current PI controllers ---
    float Vd = pidId.SetInpit({
        .feedback = dq.dAxis, .reference = 0.0f, .frequency = PWM_FREQ
    }).Resolve().Get();

    float Vq = pidIq.SetInpit({
        .feedback = dq.qAxis, .reference = IqRef, .frequency = PWM_FREQ
    }).Resolve().Get();

    // --- 7. Inverse Park + SVGEN → duty cycles ---
    invPark.SetPark({Vd, Vq, estAngle});
    invPark.ResolveInverse();
    auto vab = invPark.GetAxis();

    svg.Set(vab.alpha / (vdc / 2.0f), vab.beta / (vdc / 2.0f));
    svg.ResolveCommonMode();
    auto pwm = svg.Get();

    // Set PWM duty cycle and save for next step
    lastDutyA = (pwm.phaseA + 1.0f) * 0.5f;
    lastDutyB = (pwm.phaseB + 1.0f) * 0.5f;
    lastDutyC = (pwm.phaseC + 1.0f) * 0.5f;

    TIM_SetDutyA(lastDutyA);
    TIM_SetDutyB(lastDutyB);
    TIM_SetDutyC(lastDutyC);
}
```


### Voltage-mode FOC (without current sensors)

Like in `ServoBLDC` — simplest FOC for positioning.
**Requires only encoder, does not require current sensors.**

Principle: position PID outputs voltage amplitude, which is entirely applied
to the q-axis (via Vd=0, Vq=amplitude). Current is not controlled — limited only by amplitude.

```cpp
#include <Utilities/Math/FOC/ParkTransformation.h>
#include <Utilities/Math/FOC/SpaceVectorGenerator.h>
#include <Utilities/Math/PID/PidController.h>

// ===== Parameters =====
const uint32 POLE_PAIRS = 16;    // Pole pairs
const float Vdc = 24.0f;         // DC bus voltage [V]
const uint32 ISR_FREQ = 5000;    // 5 kHz (for positioning 1-5 kHz is sufficient)

ParkTransformation<float> invPark;
SpaceVectorGenerator<float> svg;

// Position PID — output = voltage amplitude [V]
// Roll enabled: correct handling of 359° → 1° transition
PidController<float> positionPID;

float encoderOffset = 0;         // From calibration (see CalibrateEncoder above)
float targetAngleDeg = 0;        // Target angle [degrees]

void VoltageFOC_Init() {
    positionPID
        .SetCoefficients({
            .proportional = 5.0f,
            .integral = 0.5f,
            .derivative = 0.01f
        })
        .SetOuput({
            .max = 5.0f,             // Maximum amplitude [V] (indirectly limits current)
            .min = -5.0f
        })
        .SetRoll({
            .enable = true,
            .maxInput = 360.0f,      // Cyclic data 0..360°
            .minInput = 0.0f
        });
}

// ===== PWM timer interrupt (same timer that drives the switches) =====
// Frequency: 1-5 kHz is sufficient, since position loop bandwidth
// is typically 10-100 Hz (limited by mechanical inertia).
void VoltageFOC_ISR() {
    // --- Electrical angle ---
    // Example: absolute magnetic encoder (AS5048A) returns 0..360°
    float mechAngleDeg = ReadAbsoluteEncoder();  // 0..360 [degrees]
    float mechAngleRad = (mechAngleDeg - encoderOffset) * (M_PI / 180.0f);
    float elecAngle = mechAngleRad * POLE_PAIRS;
    // elecAngle [rad] — electrical angle for Park

    // --- Position PID → voltage amplitude ---
    // Roll automatically handles the 0°/360° crossover
    float amplitude = positionPID.SetInpit({
        .feedback  = mechAngleDeg,       // Current angle [degrees]
        .reference = targetAngleDeg,     // Target angle [degrees]
        .frequency = ISR_FREQ
    }).Resolve().Get();
    // amplitude [V] — sign determines direction: + = forward, - = reverse

    // --- Inverse Park: all amplitude on q-axis ---
    // Vd=0 (no unnecessary flux), Vq=amplitude (torque)
    // No +90° shift needed — Park does this automatically:
    //   when Vd=0 and Vq=amplitude, voltage is perpendicular to rotor flux
    invPark.SetPark({0.0f, amplitude, elecAngle});
    invPark.ResolveInverse();
    auto ab = invPark.GetAxis();

    // --- SVPWM → duty cycles ---
    svg.Set(ab.alpha / (Vdc / 2.0f), ab.beta / (Vdc / 2.0f));
    svg.ResolveCommonMode();
    auto pwm = svg.Get();

    // Duty cycle: [-1,1] → [0,1]
    TIM_SetDutyA((pwm.phaseA + 1.0f) * 0.5f);
    TIM_SetDutyB((pwm.phaseB + 1.0f) * 0.5f);
    TIM_SetDutyC((pwm.phaseC + 1.0f) * 0.5f);
}

// Set target angle from main loop (main loop, UART, CAN, ...):
void SetTargetAngle(float degrees) {
    targetAngleDeg = degrees;  // 0..360
}
```