# FixedFormat

`Common/Utilities/TextNumber/FixedFormat.h`: writes a double as a fixed-point number with no exponent, the way JSON writes numbers. It has the rules of `JSON::Writer::Number(double, precision)`.

---

## Use

```cpp
char buffer[32] = {};
size_t length = FormatFixedPoint(std::span<char>(buffer), -0.0004, 6);   // "-0.000400"
size_t none = FormatFixedPoint(std::span<char>(buffer), -0.0004, 3);     // "0.000": a rounded zero has no sign
size_t refused = FormatFixedPoint(std::span<char>(buffer), 1e10, 9);     // 0: 1e10 * 10^9 reaches 9.0e18
```

---

## Rules

- The precision is clamped to 0..9. With 0 there is no point.
- The scaled value is rounded half away from zero, as `std::llround` does.
- A minus sign appears only when the rounded result is not zero.
- The function returns 0, and writes nothing, for NaN, infinity, a magnitude whose scaled value would reach 9.0e18, and a text that does not fit.

`JSON::Writer` turns a 0 into its error flag, so a caller using JSON sees the same refusals as before.

---

## Compile Time

The function is `constexpr`. It needs no libm call: the rounding is written out by hand.

```cpp
constexpr bool FixedIs() {
    char buffer[16] = {};
    size_t length = FormatFixedPoint(std::span<char>(buffer), 2.5, 0);
    return std::string_view(buffer, length) == "3";
}
static_assert(FixedIs());
```
