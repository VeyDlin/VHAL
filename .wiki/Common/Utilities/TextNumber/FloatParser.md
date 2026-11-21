# FloatParser

`Common/Utilities/TextNumber/FloatParser.h`: reads a `float` or `double`, whole or one character at a time, with the grammar of `Print::StringToFloat`.

---

## Grammar

- An optional sign: `-` always, `+` only with `WithPlusSign()`.
- Digits, with at most one `.`. `5.`, `.5` and `5.25` are all valid; a lone `.` is empty.
- An optional exponent: `e` or `E`, an optional sign straight after it, then at least one digit. `1e` reads as 1 in a whole string, and `Finish()` of a streaming parser reports `ExponentWithoutDigits`.
- `inf` and `nan` in any case, straight after the sign. Letters after them are not read, so `infinity` reads as `inf`.
- A word cut short, such as `in` or `na`, is an `InvalidCharacter` error. Its `endPosition` is the number of characters accepted (2 for `in`), although no value was read. `Print::StringToFloat` stops before the word in that case, after any blanks and sign.
- The sign of a NaN is not kept: `-nan` gives a positive quiet NaN.

`FloatOptions` has `WithPlusSign()` and `WithLeadingBlanks()`, both off by default.

---

## Streaming

`FloatParser<T>` has `Push`, `Pop` and `Finish`, as `IntegerParser` does, plus `IsExponentPending()`.

`Pop()` is exact. Each step records what it needs to step back: the digits kept in the 19-digit mantissa, the count of digits dropped beyond them, the digits after the point, and the exponent with its own count of digits and sign.

A streaming parser does not keep the text, so it cannot decide a rounding that depends on the digits it dropped. Its accuracy is the streaming bound in [Accuracy](#accuracy).

---

## Whole String

```cpp
NumberResult<double> a = ParseFloat<double>("1.5abc");   // value 1.5, endPosition 3
NumberResult<double> b = ParseFloat<double>("1e");       // value 1, endPosition 1
NumberResult<double> c = ParseFloat<double>("x");        // InvalidCharacter, position 0
NumberResult<float> d = ParseFloat<float>("3.4e39");     // value inf: beyond the range is infinity, not an error
```

Out-of-range values are not errors: a value too large becomes infinity and one too small becomes zero, as the host's `strtod` does.

---

## Accuracy

### Whole String

`ParseFloat<T>` gives the correctly rounded value (round half to even) for any number of significant digits, for `float` and `double`. A text with more than 19 digits is first rounded from its first 19 digits and the next larger mantissa. When those two candidates round to the same float, that float is the result. When they do not, the text is compared exactly with the midpoint between them, so a digit far out can still decide the rounding.

The exact comparison uses no heap and is `constexpr`. With GCC 15.2 its stack depth for `double` is 752 bytes at `-O2` (a 320-byte caller frame and 432 bytes in `CompareWithMidpoint`) and 784 bytes at `-Os`, measured with `-fstack-usage`. Other compilers and inlining choices give other figures. See the [README](README.md#accuracy).

### Streaming

`FloatParser<T>` keeps 19 significant digits and drops the rest. A mantissa of at most 19 digits gives the correctly rounded value. A longer text is within 2 ulp of the correctly rounded value, and the largest distance seen in the tests is 1 ulp. See the [README](README.md#accuracy).

---

## Compile Time

```cpp
static_assert(ParseFloat<double>("0.1").value == 0.1);
static_assert(ParseFloat<float>("0.1").value == 0.1f);
```
