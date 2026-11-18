# MathUtilities

Header-only helpers in `MathUtilities` for random integer values, min/max updates, and linear interpolation or extrapolation.

```cpp
#include <Utilities/Math/MathUtilities.h>
```

## Random

`Random(type a, type b)` returns a value using `rand() % (b - a + 1)`, offset by `a`. Use an integer type, pass bounds with `a <= b`, and ensure `b - a + 1` does not overflow or become zero.

```cpp
int value = MathUtilities::Random(1, 10);
```

## SetMin and SetMax

These functions update a data variable only when the candidate value crosses it:

- `SetMin(data, value)` assigns `value` when `value < data`.
- `SetMax(data, value)` assigns `value` when `value > data`.

The data and candidate may have different types, provided the comparison and assignment are valid. The comparison uses the original candidate; assignment converts it to the data variable's type.

When the data and the candidate are both integer types (`bool` excluded), the comparison uses `std::cmp_less` and `std::cmp_greater`. These compare the mathematical values, so a negative signed candidate is less than any unsigned data value. The assignment still converts the candidate, so a negative value assigned to an unsigned variable wraps around.

```cpp
float lowerBound = 5.0f;
MathUtilities::SetMin(lowerBound, 3); // lowerBound becomes 3.0f

float upperBound = 5.0f;
MathUtilities::SetMax(upperBound, 7); // upperBound becomes 7.0f

unsigned int count = 3u;
MathUtilities::SetMin(count, -1); // -1 < 3, so count becomes 4294967295u (wrapped)
```

## Interpolation and Extrapolation

Both functions calculate the linear value at `x2` from the points `(x0, y0)` and `(x1, y1)`. They do not clamp `x2` to the interval between `x0` and `x1`; values outside that interval extrapolate the same line.

The input x-coordinates must differ (`x0 != x1`); this is a precondition and is not checked.

```cpp
float midpoint = MathUtilities::Interpolation(0.0f, 10.0f, 4.0f, 18.0f, 2.0f);
float extended = MathUtilities::Extrapolation(0.0f, 10.0f, 4.0f, 18.0f, 6.0f);
```

All arguments and the return value use one deduced type. Use floating-point arguments when the result needs fractional precision.

For integral types of up to 64 bits, signed or unsigned, every intermediate value is exact: the differences are computed without overflow, the product is 128 bits wide, and the division truncates toward zero. The 128-bit arithmetic is written in portable code, so no `__int128` is needed. A result outside the range of the argument type saturates to that type's minimum or maximum; it does not wrap.

```cpp
int32_t low = MathUtilities::Interpolation<int32_t>(INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX, 0); // 0
uint64_t high = MathUtilities::Extrapolation<uint64_t>(0u, 0u, 1u, UINT64_MAX, 2u);                 // UINT64_MAX (saturated)
```
