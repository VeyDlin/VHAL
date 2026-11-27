# FloatFormat

`Common/Utilities/TextNumber/FloatFormat.h`: writes a `float` or `double` as text, with the rules of `Print::FloatToString`.

---

## Use

```cpp
char buffer[64] = {};
size_t length = FormatFloat<double>(std::span<char>(buffer), 2.675, 2);   // "2.67": exact binary value, half away from zero
size_t big = FormatFloat<double>(std::span<char>(buffer), 1e20, 2);        // "1.00e+20"
```

The value may be a `float` or a `double`. The precision is the number of digits after the point, at most 40 (larger values are clamped). The default is 2.

---

## Rules

- Fixed point below 2^64, with the exact binary value rounded half away from zero. Rounding happens on the exact value, so `2.675` at precision 2 is `2.67`.
- From 2^64 up, scientific form `d.ddde+N`.
- A negative value keeps its minus sign even when it rounds to zero: `-1e-300` at precision 0 is `-0`.
- `NaN`, `Inf` and `-Inf` are written as text.

---

## Buffers

Like the integer formatter, it writes the whole text or nothing. It returns 0 when the text does not fit. Print's `FloatToString` truncates and terminates instead.

---

## Compile Time

The conversion is `constexpr`. It reads the exponent and mantissa of a double with `std::bit_cast`. Under `-std=c++23`, with g++ 15.2 and with GCC 14.2 for riscv32, `std::frexp` on a double and `std::ldexp` are usable in a constant expression, so the reading could use `std::frexp`. `std::frexp` on a float is not usable there.
