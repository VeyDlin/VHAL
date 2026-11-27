# IQMath — Fixed-Point Arithmetic Library

A fixed-point arithmetic library in the style of TI IQmath. All values are stored as `int32`, the `Q` parameter specifies the number of fractional bits. Transparently substituted as a template type in FOC, PID, and other classes instead of `float`/`double`.

## Table of Contents

- [Quick Start](#quick-start)
- [IQ Format](#iq-format)
- [Construction and Conversion](#construction-and-conversion)
- [Arithmetic](#arithmetic)
- [Math Functions](#math-functions)
  - [sin / cos](#sin--cos)
  - [sqrt](#sqrt)
  - [atan2](#atan2)
  - [exp and expm1](#exp-and-expm1)
  - [abs / fabs / min / max / clamp / floor / ceil / round / copysign](#abs--fabs--min--max--clamp--floor--ceil--round--copysign)
- [Conversion Between IQ Formats](#conversion-between-iq-formats)
- [Concepts IQType / RealType](#concepts-iqtype--realtype)
- [Number Range](#number-range)
- [LUT Configuration](#lut-configuration)
- [Usage with Template Classes (ADL)](#usage-with-template-classes-adl)
- [Examples](#examples)
  - [IQ in FOC](#iq-in-foc)
  - [IQ in PID](#iq-in-pid)
- [Format Reference](#format-reference)

## Quick Start

```cpp
#include <Utilities/Math/IQMath/IQMath.h>

// Create a value in IQ24 format (8-bit integer part + 24-bit fractional part)
iq24 voltage(3.14f);
iq24 current(1.5f);

// Arithmetic — just like with float
iq24 power = voltage * current;

// Trigonometry
iq24 angle(0.785f);  // pi/4
iq24 s = sin(angle);
iq24 c = cos(angle);

// Back to float
float result = power.ToFloat();  // 4.71
```

## IQ Format

`IQ<Q>` stores a number in `int32`, where the `Q` least significant bits are the fractional part:

```
value = raw / 2^Q
```

| Format | Q | Integer part (bits) | Range | Precision |
|--------|---|---------------------|-------|-----------|
| `IQ<8>` / `iq8` | 8 | 23 | ±8 388 608 | ~0.004 |
| `IQ<16>` / `iq16` | 16 | 15 | ±32 768 | ~0.000015 |
| `IQ<24>` / `iq24` | 24 | 7 | ±128 | ~0.00000006 |

Q can range from 1 to 30. The larger Q is, the higher the precision, but the narrower the range.

For FOC, `IQ<24>` is recommended (precision comparable to float, range of ±128 is sufficient for normalized quantities).

## Construction and Conversion

```cpp
// From a float or double constant — converted when the program is compiled
iq24 a = 3.14f;
iq24 b(1.5);

// From a run-time float — FromFloat. A run-time float does not convert implicitly.
volatile float sample = 0.25f;
iq24 c = iq24::FromFloat(sample);

// Out-of-range float/double saturate to the type's maximum or minimum; NaN converts to 0.
iq24 big(1e30);   // raw = INT32_MAX

// From integer — explicit conversion
iq24 c(IQ<24>(5));

// From raw int32
iq24 d = IQ<24>::FromRaw(16777216);  // = 1.0 in IQ24 (2^24 = 16777216)

// Back to scalar types
float  f = a.ToFloat();
int    i = a.ToInt();      // integer part (discards fractional part)

// Explicit cast operators — integer types and float
float  f2 = static_cast<float>(a);
uint32 u  = static_cast<uint32>(a);
int16  s  = static_cast<int16>(a);
```

Converting a float or double rounds to the nearest LSB, ties away from zero: `IQ<16>(1.5 / 65536.0)` is 2 LSB and `IQ<16>(-1.5 / 65536.0)` is -2 LSB. Out-of-range values saturate, and NaN converts to 0. The conversion from a float or double is `consteval`: a constant converts, and a run-time value does not compile.

Run-time values use `IQ<Q>::FromFloat(float)` (the same rounding and saturation) or `RealFrom<T>(value)`, which gives a float for a float `T` and an IQ for an IQ `T`. For an IQ `T` the value must be a float: `RealFrom<IQ<16>>(doubleValue)` does not compile, and `RealFrom<IQ<16>>(static_cast<float>(doubleValue))` is the explicit form. The compile error for a run-time float reads `call to consteval function 'IQ<16>(rt)' is not a constant expression`; the fix is `FromFloat`.

Conversion to an integer type drops the fraction (rounding toward negative infinity) and saturates to the limits of the target type, so `static_cast<uint8>(iq16(300.0f))` is 255 and `static_cast<uint32>(iq16(-1.0f))` is 0. `ToInt()` returns the integer part as `int32`; it cannot overflow.

## Arithmetic

All standard operations. Multiplication goes through an `int64` intermediate and rounds to nearest:

```cpp
iq24 a(10.0f);
iq24 b(3.0f);

iq24 sum  = a + b;    // 13.0
iq24 diff = a - b;    // 7.0
iq24 prod = a * b;    // 30.0 (via int64 intermediate result)
iq24 quot = a / b;    // 3.333...
iq24 neg  = -a;       // -10.0

// Compound assignment
a += b;
a -= b;
a *= b;
a /= b;

// Arithmetic with integer scalars
iq24 doubled = a * 2;
iq24 halved  = a / 2;
iq24 tripled = 3 * a;
iq24 inc     = a + 1;
iq24 dec     = a - 1;

// Comparisons
if (a > b) { ... }
if (a == b) { ... }
if (a <= b) { ... }
```

Addition, subtraction and multiplication saturate: a result outside the raw `int32` range becomes the maximum or minimum of the type instead of wrapping. This covers `IQ` with `IQ`, `IQ` with an integer scalar (in either order), and the compound forms `+=`, `-=`, `*=` and `/=`. The integer constructor saturates the same way, so `iq24(200)` is the maximum of `iq24`, about 128.

Division by zero does not trap: `x / 0` saturates to the maximum for positive `x` and to the minimum for negative `x`; `0 / 0` returns 0. Division results that do not fit the type also saturate.

Negating or taking `abs` of the most negative raw value saturates to the maximum.

Multiplication and division round to the nearest LSB:

- `IQ * IQ` adds half an LSB before the shift, so a tie (exactly half an LSB) goes toward +infinity. A raw product of -1.5 LSB gives -1 LSB, and +1.5 LSB gives 2 LSB.
- `IQ / IQ` and `IQ / integer` round to nearest with ties away from zero. `IQ<16>(1) / IQ<16>(3)` is 21845 LSB (21845.33), and 2.5 LSB divided by 2 is 3 LSB.

### Scalars are integers

An integer scalar (`int`, `uint8`, `int64` and the other integral types) works with `+`, `-`, `*`, `/` and the compound forms. The scalar itself is exact, and `+`, `-` and `*` are exact unless the result saturates. `/` rounds to nearest, as `IQ / IQ` does (`IQ<16>(1) / 3` is 21845 LSB). An unsigned 64-bit value above `INT64_MAX` is treated as `INT64_MAX`.

A `float` or `double` constant works on the right of an operator, where it converts to IQ at compile time. A constant on the left does not compile, and a run-time float does not compile on either side. Put the constant on the right, and pass a run-time value through `FromFloat`:

```cpp
iq16 x(4);
constexpr float kScale = 2.7f;

// iq16 bad = 2.7f * x;          // compile error: no match for 'operator*'
iq16 good = x * 2.7f;            // about 10.8: the constant is rounded to IQ at compile time
iq16 scaled = x * kScale;        // a named constant works as well

volatile float runTime = 2.7f;
// iq16 wrong = x * runTime;     // compile error: call to consteval function is not a constant expression
iq16 fine = x * iq16::FromFloat(runTime);
```

### Float values and soft float

The rules for a `float` in IQ code:

- A float or double constant converts when the program compiles. It goes on the right of an operator (`x * 2.7f`), never on the left.
- A run-time float converts with `IQ<Q>::FromFloat(value)`. `RealFrom<T>(value)` takes a float for an IQ `T`, and a double only for a float `T`.

The object check, `Common/Utilities/Math/IQMath/.tool/sim/ObjectCheck.sh`, builds the samples in `.tool/sim/ObjectCheck/` for Cortex-M0+ with `-Os`. The samples are IQ add, multiply, divide, sqrt, sin, exp, an `Animation` step and a `LowPassFilter` step, all in `IQ<16>`. The check fails when an object references a soft-float or soft-double helper (`__aeabi_f*` or `__aeabi_d*`). `run.sh` runs it. The control sample `control_float.cpp` multiplies two run-time floats and must reference `__aeabi_fmul`; if it does not, the check cannot see soft float and fails.

The double tokens in `IQTrig.h` (`TaylorSin`, the sine table, `twoPiValue` and `RadiansToPhase`) are evaluated by the compiler. The sine table is placed in flash as `int32` data (1028 bytes for `LutSize` 256), and no function that takes a double is emitted in the sine object. A run-time call to `RadiansToPhase` would reference the soft-double helpers, so pass a constant angle to it.

## Math Functions

All functions are free-standing (not methods) and work via ADL. This allows using IQ as a drop-in replacement for float in template classes.

### sin / cos

The argument is in radians, of any size. It becomes a binary angle first: a `uint32` phase where 2^32 is one turn. The uint32 wrap is the reduction, so the code has no `%` and no division. A quarter-wave table of Q31 values (`L + 1` entries, generated at compile time, in flash) gives sin and cos of the table point, and a second-order Taylor step from that point gives the value. The result is rounded to the nearest LSB.

```cpp
iq24 angle(1.5708f);  // pi/2

iq24 s = sin(angle);  // ≈ 1.0
iq24 c = cos(angle);  // ≈ 0.0
```

**Radians to phase.** `phase = raw * 2^(32 - Q) / (2 pi)`, computed as `raw * M >> (Q + 2)` with `M = round(2^33 / pi) = 2734261102`. That is one 64-bit product. The rounding of `M` moves the phase by at most `2^(28 - Q)` units, and one unit is `2 pi / 2^32` rad, so the error is `2 pi / 16 = 0.393` LSB at every `Q`. The final shift adds half a unit more, which is about 0.012 LSB at Q24. The whole int32 range is covered, from `-128` to `128` rad at `Q24`.

**Second-order step.** With `a` the table point and `t` the distance from it (below `2 pi / 1024` for the default table), `sin(a + t) = sin(a) (1 - t^2 / 2) + cos(a) (t - t^3 / 6)`. Both `sin(a)` and `cos(a)` come from the same quarter table: `cos(a) = sin(pi/2 - a)` is entry `L - index`. The next term, `t^4 / 24`, is below `2^-33` for the default table. Linear interpolation alone gives about 80 LSB at Q24, and this step removes that error.

**Accuracy.** The error is in LSB of the result format and includes the final rounding, against `std::sin` and `std::cos` in double at the exact input. The default table (`LutSize` 256):

| Q | Inputs | sin worst (LSB) | cos worst (LSB) |
|---|---|---|---|
| 16 | every raw value in [-4 pi, 4 pi] | 0.5001 | 0.5002 |
| 16 | every raw value in the int32 range (step 7, sampled) | 0.7398 | 0.7400 |
| 20 | step 7 over [-4 pi, 4 pi] | 0.5030 | 0.5031 |
| 20 | every raw value in the int32 range (step 9, sampled) | 0.7408 | 0.7414 |
| 24 | every raw value in [-4 pi, 4 pi] | 0.5617 | 0.5588 |
| 24 | every raw value in the int32 range (step 5, sampled) | 0.7741 | 0.7773 |

The worst errors come from the phase conversion and the rounding. The bound at Q24 over the whole int32 range is the sum of these terms: 0.393 LSB for the phase conversion, 0.012 LSB for its final shift, 0.5 LSB for the rounding to Q, and about 0.016 LSB for the Q30 truncation of the sum. That is 0.921 LSB, so the bound is 0.93 LSB, and the result is within 1 LSB at every raw value. Phases (`SinPhase`, `CosPhase`) have no conversion error, so they are at the rounding floor: 0.5000 LSB at Q16 and 0.5329 LSB at Q24 over the dense sweep. The `cos(x)` and `sin(x + pi/2)` identity holds within one LSB (the constant `pi/2` is rounded to Q, which moves the angle by up to half an LSB).

The 1 LSB target holds up to Q24. Above it the error grows, because the phase unit is `2 pi / 2^32` rad: the radians path reaches about 1.07 LSB at Q27, 1.43 at Q28 and 3.14 at Q30 (worst of 20 million random raw values), and phases reach about 2.5 LSB at Q30.

The test `.tool/sim/TestTrig.cpp` uses coarser steps than the table: step 1009 over the int32 range at Q16, step 7 at Q20 and step 97 at Q24 over [-4 pi, 4 pi]. Each of its bounds is the worst error of its sweep plus 0.02 LSB, rounded up to 0.01, except the Q24 [-4 pi, 4 pi] bound, which is the exhaustive run in the table plus the same margin.

**Cost.** On a Cortex-M4 at `-O2`, with `-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16`, `SinPhase<24>` is 36 instructions and `sin(iq24)` is 44, both with no call. On a Cortex-M0+ the 64-bit products become `__aeabi_lmul`: `SinPhase<24>` is 95 instructions with 7 calls, and `sin(iq24)` is 106 with 8. The sine path calls no `__aeabi_ldivmod`; IQ division (`DivideRaw`) does, because it divides 64-bit values, and on a Cortex-M0+ that is a library call. The table is 1 KB of flash for `LutSize` 256.

**Binary angles.** `SinPhase<Q>(uint32 phase)` and `CosPhase<Q>(uint32 phase)` take the phase directly, for code that keeps an integer angle (FOC, wave generators, oscillators). They return `IQ<Q>` with the default table size. `RadiansToPhase(double)` is a `constexpr` helper that converts an angle in radians to a phase, rounded to the nearest unit:

```cpp
constexpr uint32 quarterTurn = RadiansToPhase(3.14159265358979 / 2.0);  // 0x40000000

iq24 wave = SinPhase<24>(phase);    // sin of the binary angle
iq24 other = CosPhase<24>(phase);   // cos of the same angle
```

Each unit of phase is `2 pi / 2^32` rad, so a full turn is 2^32 units and wraps to 0. `RadiansToPhase` takes angles of any size: it takes the fraction of a turn in double, so its error is about 2^-53 of the turns (for example about 1e-5 turn at 1e12 rad). Beyond 2^52 turns, and for NaN and infinity, it returns 0.

**Float: `FastSin` and `FastCos`.** An opt-in float sine and cosine without libm, in `Utilities/Math/IQMath/FastTrig.h`. The argument is reduced by `k pi/2` with a Cody-Waite split of `pi/2` into three parts, which keeps the reduction exact for `|x|` below about 2.5e4. A Taylor polynomial (degree 9 for sine, degree 10 for cosine) then gives the value on `[-pi/4, pi/4]`. Measured against `std::sin` in double on `[-1e4, 1e4]` (step 1e-3), the largest error is 8.5e-8. At multiples of `pi/2` it is 3e-8. On a Cortex-M4 at `-O2`, `FastSin` is 74 instructions and `FastCos` is 68 (static count, with the checks for NaN and for the argument range; 49 and 51 without them), with no call. `std::sin` stays the default; code opts in by calling `FastSin` and `FastCos`:

```cpp
#include <Utilities/Math/IQMath/FastTrig.h>

float s = FastSin(theta);
float c = FastCos(theta);
```

Domain: the error stays at the 8.5e-8 level up to about 2.5e4. Between 2.5e4 and 2.6e4 it rises to about 5e-7, and it keeps growing with `|x|`. Beyond about 1.6e6 the argument is clamped before the reduction, so the result stays in [-1, 1] but is not the sine of `x`. NaN and infinity give NaN.

### sqrt

Newton-Raphson, 6 iterations on 64-bit arithmetic:

```cpp
iq24 val(2.0f);
iq24 root = sqrt(val);  // ≈ 1.4142
```

Returns 0 for negative and zero arguments.

### atan2

9th-order polynomial approximation (minimax), precision ~0.0005 rad:

```cpp
iq24 y(1.0f);
iq24 x(1.0f);
iq24 angle = atan2(y, x);  // ≈ pi/4 = 0.7854
```

Correctly handles all four quadrants and special cases (x=0, y=0).

### exp and expm1

`exp(x)` and `expm1(x) = exp(x) - 1` split `x = k ln 2 + r` with `|r|` at most about 0.35, evaluate the series for `exp(r) - 1` in 30 fraction bits, and shift by `k`. `expm1` is accurate for small arguments, so `1 - exp(-x)` keeps its precision:

```cpp
iq16 val(1.0f);
iq16 e = exp(val);  // ≈ 2.7183
```

```cpp
iq16 x(-0.001f);       // raw -66 LSB
iq16 a = 1 - exp(x);   // 0.001: raw 66 LSB
iq16 b = expm1(x);     // -0.001: raw -66 LSB
```

The range limits the results: `IQ<16>` saturates for arguments above about 10.4, and `IQ<24>` (range ±128) above about 4.85. The result is 0 once `exp(x)` is below half an LSB, so `exp` returns 0 and `expm1` returns -1 for arguments below about -(Q + 1) ln 2 (about -11.8 at `IQ<16>`).

Measured against `std::exp` and `std::expm1` in double. For each Q, every raw value from the underflow limit to the overflow limit of the format was measured (every raw value, not a sample), and the worst errors are:

| Q | exp, worst error (LSB) | expm1, worst error (LSB) | expm1 for \|x\| < 0.5, worst error (LSB) |
|---|---|---|---|
| 8 | 1.11 | 1.11 | 0.50 |
| 12 | 1.46 | 1.46 | 0.50 |
| 16 | 1.81 | 1.81 | 0.50 |
| 20 | 2.15 | 2.15 | 0.50 |
| 24 | 1.92 | 1.92 | 0.52 |
| 28 | 1.70 | 1.70 | 0.85 |
| 30 | 1.58 | 3.28 | 1.58 |

The test `.tool/sim/TestExp.cpp` sets each bound to the worst error above plus 0.25 LSB, rounded up to 0.01. The bounds are 1.37, 1.71, 2.06, 2.40, 2.17, 1.96 and 3.54 LSB for exp and expm1 over the whole range, and 0.75, 0.75, 0.76, 0.76, 0.78, 1.11 and 1.83 LSB for expm1 with `|x| < 0.5`, for Q8, Q12, Q16, Q20, Q24, Q28 and Q30. The test runs Q8, Q12, Q16 and Q20 on every raw value. It samples Q24, Q28 and Q30 with steps 64, 512 and 640 to keep the test fast, so it finds an error above a bound only when a sampled value reaches it. The errors include the final rounding to Q bits (up to 0.5 LSB). The error grows with the value, because the series keeps 30 fraction bits and `2^k` scales its error. At Q30, expm1 is worst at x = 1.04, where the result is 2 times the series.

### abs / fabs / min / max / clamp / floor / ceil / round / copysign

```cpp
iq24 a(-5.3f);
iq24 b(3.0f);

iq24 r1 = abs(a);                   // 5.3
iq24 r2 = fabs(a);                  // 5.3 (alias for abs)
iq24 r3 = min(a, b);                // -5.3
iq24 r4 = max(a, b);                // 3.0
iq24 r5 = clamp(a, iq24(-2), b);    // -2.0
iq24 r6 = floor(iq24(3.7f));        // 3.0
iq24 r7 = ceil(iq24(3.2f));         // 4.0
iq24 r8 = round(iq24(3.5f));        // 4.0
iq24 r9 = copysign(b, a);           // -3.0
```

## Conversion Between IQ Formats

Explicit conversion between Q values. Converting to a larger `Q` (more fractional bits) shifts the raw value left and saturates when it does not fit. Converting to a smaller `Q` drops the low bits, which rounds toward negative infinity:

```cpp
iq24 precise(3.14f);

// Downward conversion (losing fractional bits)
iq16 medium(precise);   // explicit
iq8  coarse(precise);

// Upward conversion (adding zero bits)
iq24 restored(medium);  // 3.14 → 3.14 (with IQ16 precision loss)

// LutSize does not affect conversion
IQ<24, 512> hq(precise);  // also works
```

## Concepts IQType / RealType

C++20 concepts for constraining template parameters:

```cpp
// IQType — matches any IQ<Q, L>
template<IQType T>
void ProcessFixed(T value);

// RealType — matches float, double, or any IQ<Q, L>
template<RealType T>
class PidController { ... };
```

Usage in template classes:

```cpp
template <RealType Type = float>
class PidController {
    // Works with float (default), double, iq16, iq24, etc.
};

PidController pid;           // float
PidController<iq24> pid;     // IQ<24>
```

## Number Range

`NumberRange<T>::maxAbs` is the largest magnitude a number type holds, as a `double`: the largest finite value for `float` and `double`, and `2^31 / 2^Q` for `IQ<Q>` (32768 for `IQ<16>`, 128 for `IQ<24>`). It is declared in `Common/Utilities/Math/NumberRange.h`.

A class that needs a range states it with the concept `RangeAtLeast<T, Need>`:

```cpp
#include <Utilities/Math/NumberRange.h>

template<typename T>
requires RangeAtLeast<T, 40000.0>
class SpeedPeriod {
    // ...
};

SpeedPeriod<float> ok;     // float holds about 3.4e38
SpeedPeriod<iq16> bad;     // error: constraints not satisfied (IQ<16> holds up to 32768)
```

`HasRange<T>` is false for types without a range, such as integers. The trait does not change any class by itself: a class adopts it through its own `requires` clause.

## LUT Configuration

The second template parameter `LutSize` sets the sin/cos table size. Must be a power of two, >= 16:

```cpp
// Default: 256 points — 1 KB ROM, sin and cos within 0.6 LSB at Q24 (measured over [-4 pi, 4 pi])
using iq24 = IQ<24>;

// Compact: 64 points — 256 bytes ROM, worst 0.78 LSB at Q24 over [-4 pi, 4 pi] (0.87 over the int32 range, sampled)
using iq24_compact = IQ<24, 64>;

// High-precision: 1024 points — 4 KB ROM, worst 0.56 LSB at Q24 over [-4 pi, 4 pi], the same as 256 because the phase
// conversion and the rounding set the error there
using iq24_hq = IQ<24, 1024>;
```

The table size matters for `sin` and `cos` only through the Taylor step (see [sin / cos](#sin--cos)). The float `FastSin` and `FastCos` have no table.

Below 64 points the Taylor step leaves too much error. At Q24 over [-4 pi, 4 pi], 32 points give 4.5 LSB and 16 points give 65 LSB. The 1 LSB bound therefore needs `LutSize` >= 64. The `LutSize` check enforces only a power of two and at least 16, so a table of 16 to 32 points compiles and loses accuracy without a warning.

Each `LutSize` creates its own table. If only one size is used in the project, only one table ends up in ROM.

`IQ<24>` and `IQ<24, 512>` are **different types**. They cannot be mixed in arithmetic without explicit conversion.

## Usage with Template Classes (ADL)

FOC classes (and any template classes) use the ADL pattern for calling math functions:

```cpp
template<typename T>
class ParkTransformation {
    ParkTransformation& Resolve() {
        using std::cos;  // Fallback for float/double
        using std::sin;

        // If T = float  → std::cos, std::sin will be called
        // If T = IQ<24> → ::cos(IQ<24>), ::sin(IQ<24>) will be called
        dq.d = cos(theta) * ab.alpha + sin(theta) * ab.beta;
        dq.q = cos(theta) * ab.beta  - sin(theta) * ab.alpha;
        return *this;
    }
};
```

For this to work, the functions `sin`, `cos`, `sqrt`, `atan2`, `exp`, `abs`, `fabs`, `min`, `max`, `clamp`, `floor`, `ceil`, `round`, `copysign` just need to be visible to the compiler when the template is instantiated. This is ensured by including `IQMath.h` before using the template class.

## Examples

### IQ in FOC

```cpp
#include <Utilities/Math/IQMath/IQMath.h>
#include <Utilities/Math/FOC/ClarkeTransformation.h>
#include <Utilities/Math/FOC/ParkTransformation.h>
#include <Utilities/Math/FOC/SpaceVectorGenerator.h>

using T = iq24;

ClarkeTransformation<T> clarke;
ParkTransformation<T> park;
SpaceVectorGenerator<T> svgen;

// In ISR (20 kHz):
void FOC_ISR() {
    T ia = T(AdcToAmps(ADC_PHASE_A));
    T ib = T(AdcToAmps(ADC_PHASE_B));
    T theta = T(GetElectricalAngle());

    clarke.SetPhase({.a = ia, .b = ib})
          .Resolve(ClarkeTransformation<T>::CurrentsMode::C2);

    auto ab = clarke.GetClarke();

    park.SetAxis({.alpha = ab.alpha, .beta = ab.beta, .angle = theta})
        .Resolve();

    auto dq = park.GetPark();

    // ... PID on dq currents ...

    park.SetPark({.dAxis = vd, .qAxis = vq, .angle = theta})
        .ResolveInverse();

    auto vab = park.GetAxis();

    svgen.Set(vab.alpha, vab.beta)
         .Resolve();

    auto pwm = svgen.Get();
    SetPWM(pwm.phaseA.ToFloat(), pwm.phaseB.ToFloat(), pwm.phaseC.ToFloat());
}
```

### IQ in PID

```cpp
#include <Utilities/Math/IQMath/IQMath.h>
#include <Utilities/Math/PID/PidController.h>

PidController<iq24> speedPid;
speedPid.SetCoefficients({iq24(0.5f), iq24(0.01f), iq24(0.0f)});
speedPid.SetOuput({.max = iq24(10.0f), .min = iq24(-10.0f)});

// In the control loop:
iq24 output = speedPid
    .SetInpit({
        .feedback  = currentSpeed,
        .reference = targetSpeed,
        .frequency = 1000
    })
    .Resolve()
    .Get();
```

## Format Reference

| Alias | Type | Storage | Q | Integer (bits) | Range | Precision | ROM (LUT) |
|-------|------|---------|---|----------------|-------|-----------|-----------|
| `iq8` | `IQ<8>` | `int32` | 8 | 23 | ±8 388 608 | 0.00391 | 1 KB |
| `iq16` | `IQ<16>` | `int32` | 16 | 15 | ±32 768 | 0.0000153 | 1 KB |
| `iq24` | `IQ<24>` | `int32` | 24 | 7 | ±128 | 0.0000000596 | 1 KB |
