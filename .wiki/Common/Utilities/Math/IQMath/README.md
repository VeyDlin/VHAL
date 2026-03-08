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
  - [exp](#exp)
  - [abs / fabs / min / max / clamp / floor / ceil / round / copysign](#abs--fabs--min--max--clamp--floor--ceil--round--copysign)
- [Conversion Between IQ Formats](#conversion-between-iq-formats)
- [Concepts IQType / RealType](#concepts-iqtype--realtype)
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
| `IQ<8>` / `iq8` | 8 | 23 | ±4 194 304 | ~0.004 |
| `IQ<16>` / `iq16` | 16 | 15 | ±16 384 | ~0.000015 |
| `IQ<24>` / `iq24` | 24 | 7 | ±128 | ~0.00000006 |

Q can range from 1 to 30. The larger Q is, the higher the precision, but the narrower the range.

For FOC, `IQ<24>` is recommended (precision comparable to float, range of ±128 is sufficient for normalized quantities).

## Construction and Conversion

```cpp
// From float/double — implicit conversion
iq24 a = 3.14f;
iq24 b(1.5);

// From integer — explicit conversion
iq24 c(IQ<24>(5));

// From raw int32
iq24 d = IQ<24>::FromRaw(16777216);  // = 1.0 in IQ24 (2^24 = 16777216)

// Back to scalar types
float  f = a.ToFloat();
double d = a.ToDouble();
int    i = a.ToInt();      // integer part (discards fractional part)

// Explicit cast operators — all standard types supported
float  f2 = static_cast<float>(a);
uint32 u  = static_cast<uint32>(a);
int16  s  = static_cast<int16>(a);
```

## Arithmetic

All standard operations; multiplication via `int64` — no loss of precision:

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

## Math Functions

All functions are free-standing (not methods) and work via ADL. This allows using IQ as a drop-in replacement for float in template classes.

### sin / cos

Implementation via LUT (Look-Up Table) with linear interpolation. The table stores a quarter period in Q31, generated at compile time (`constexpr`).

```cpp
iq24 angle(1.5708f);  // pi/2

iq24 s = sin(angle);  // ≈ 1.0
iq24 c = cos(angle);  // ≈ 0.0
```

Argument is in radians, any range (automatic normalization to [0, 2π)).

Precision depends on the LUT size (default is 256 points ≈ 14 bits). See [LUT Configuration](#lut-configuration).

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

### exp

5th-order Taylor + range reduction via 2^k:

```cpp
iq16 val(1.0f);
iq16 e = exp(val);  // ≈ 2.7183
```

For `exp`, `IQ<16>` is recommended — the range of ±16384 allows storing results for arguments up to ~9.7. With `IQ<24>`, the range is ±128, which limits `exp` to arguments up to ~4.8.

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

Explicit conversion between different Q values via bit shift:

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

## LUT Configuration

The second template parameter `LutSize` sets the sin/cos table size. Must be a power of two, >= 16:

```cpp
// Default: 256 points — 1 KB ROM, precision ~14 bits
using iq24 = IQ<24>;

// Compact: 64 points — 256 bytes ROM, precision ~12 bits
using iq24_compact = IQ<24, 64>;

// High-precision: 1024 points — 4 KB ROM, precision ~16 bits
using iq24_hq = IQ<24, 1024>;
```

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

    park.SetClarke({.alpha = ab.alpha, .beta = ab.beta})
        .SetAngle(theta)
        .Resolve();

    auto dq = park.GetDQ();

    // ... PID on dq currents ...

    park.SetDQ({.d = vd, .q = vq})
        .ResolveInverse();

    auto vab = park.GetClarke();

    T vdc = T(GetVdc());
    svgen.SetAlphaBeta(vab.alpha, vab.beta)
         .SetVdc(vdc)
         .Resolve();

    auto pwm = svgen.GetPhase();
    SetPWM(pwm.a.ToFloat(), pwm.b.ToFloat(), pwm.c.ToFloat());
}
```

### IQ in PID

```cpp
#include <Utilities/Math/IQMath/IQMath.h>
#include <Utilities/Math/PID/PidController.h>

PidController<iq24> speedPid;
speedPid.SetCoefficients(iq24(0.5f), iq24(0.01f), iq24(0.0f));
speedPid.SetOuput({.min = iq24(-10.0f), .max = iq24(10.0f)});

// In the control loop:
iq24 output = speedPid
    .SetInpit({
        .feedback  = currentSpeed,
        .reference = targetSpeed,
        .frequency = iq24(1000.0f)
    })
    .Resolve()
    .Get();
```

## Format Reference

| Alias | Type | Storage | Q | Integer (bits) | Range | Precision | ROM (LUT) |
|-------|------|---------|---|----------------|-------|-----------|-----------|
| `iq8` | `IQ<8>` | `int32` | 8 | 23 | ±4 194 304 | 0.00391 | 1 KB |
| `iq16` | `IQ<16>` | `int32` | 16 | 15 | ±16 384 | 0.0000153 | 1 KB |
| `iq24` | `IQ<24>` | `int32` | 24 | 7 | ±128 | 0.0000000596 | 1 KB |
