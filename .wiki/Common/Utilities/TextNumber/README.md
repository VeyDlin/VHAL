# TextNumber

Reads and writes numbers as text for embedded systems (VHAL), with no heap and no exceptions. Parsing reads one character at a time and can take back the last one, which is what a command line needs. The same grammars are used for whole strings, so there is one implementation of each rule. Everything parses and formats integers at compile time, and parsing and formatting of floats are constant expressions too.

---

## Table of Contents

- [Quick Start](#quick-start)
- [Headers](#headers)
- [Parsing Integers](#parsing-integers)
- [Parsing Floats](#parsing-floats)
- [Errors](#errors)
- [Formatting](#formatting)
- [Accuracy](#accuracy)
- [Compile-Time Use](#compile-time-use)
- [Tests](#tests)

---

## Quick Start

```cpp
#include <Utilities/TextNumber/IntegerParser.h>
#include <Utilities/TextNumber/FloatParser.h>
#include <Utilities/TextNumber/IntegerFormat.h>

// Whole string: the value, or the error, and how far the text was read
NumberResult<uint8_t> level = ParseInteger<uint8_t>("200", IntegerOptions<uint8_t>{}.WithRange(0, 100));
if (!level.IsOk()) {
    // level.error.code == NumberErrorCode::OutOfRange, level.error.maximum == 100
}

// One character at a time, with Backspace
IntegerParser<uint8_t> parser;
parser.Push('2');
parser.Push('5');
parser.Push('6');   // 256: accepted, out of range only at Finish
parser.Pop();       // back to 25
uint8_t value = parser.Finish().value;

// Text out of a value; the buffer is never touched when the text does not fit
char buffer[32] = {};
size_t length = FormatInteger<int32_t>(std::span<char>(buffer), -42, NumberBase::Decimal);
```

Parsing and formatting need only the standard library. Include `TextNumber.h` for everything, or only the headers you use: a file that needs integers alone includes `IntegerParser.h` and `IntegerFormat.h` and stays free of float code.

---

## Headers

| Header | What it holds | Page |
|---|---|---|
| `NumberError.h` | `NumberErrorCode`, `NumberError<T>`, `NumberResult<T>` | [Errors](#errors) |
| `IntegerParser.h` | `IntegerOptions<T>`, `IntegerParser<T>`, `ParseInteger<T>` | [IntegerParser](IntegerParser.md) |
| `FloatParser.h` | `FloatOptions`, `FloatParser<T>`, `ParseFloat<T>` | [FloatParser](FloatParser.md) |
| `IntegerFormat.h` | `NumberBase`, `FormatInteger<T>` | [IntegerFormat](IntegerFormat.md) |
| `FloatFormat.h` | `FormatFloat<T>` (the Print rules) | [FloatFormat](FloatFormat.md) |
| `FixedFormat.h` | `FormatFixedPoint` (the JSON rules) | [FixedFormat](FixedFormat.md) |
| `TextNumber.h` | all of the above | |

---

## Parsing Integers

`IntegerOptions<T>` sets the rules for one type. Each setter returns a changed copy, so a whole set is one constant expression:

```cpp
constexpr IntegerOptions<int16_t> kOptions = IntegerOptions<int16_t>{}.WithHexPrefix().WithPlusSign().WithRange(-500, 500);
```

- Prefixes `0x` and `0b`: off by default; on with `WithHexPrefix()` and `WithBinaryPrefix()`.
- `WithPlusSign()`: a leading `+` is allowed. Off by default.
- The minus sign is for signed types. `WithoutMinusSign()` turns it off for a signed type too.
- `WithRange(low, high)`: the accepted range. The default is the type's range.
- `WithLeadingBlanks()`: whole-string helpers skip leading spaces and tabs. A streaming parser has no such step.

`ParseInteger<T>(text, options)` reads the longest prefix of `text` that the grammar accepts, so `"12abc"` gives 12 with an end position of 2. Use `endPosition` to see how far it got.

`IntegerParser<T>` reads the same way one character at a time:

| Call | What it does |
|---|---|
| `Push(char)` | Returns a `NumberError<T>`: the code is `None` when the character was accepted. A refused character changes nothing. |
| `Pop()` | Undoes the last accepted character exactly. An empty parser ignores it. |
| `Finish()` | Returns the `NumberResult<T>`: the value, or the error, and the number of characters accepted. |

Digits that would overflow the 64-bit magnitude are counted and not stored, so `Pop()` removes them before it removes stored digits. The result is exact for any number of digits, and any value with extra digits is out of range.

---

## Parsing Floats

`FloatOptions` has `WithPlusSign()` and `WithLeadingBlanks()`, with the same meaning as for integers. `ParseFloat<T>(text, options)` takes `float` or `double`. Its grammar is that of `Print::StringToFloat`: an optional sign, digits with at most one point, an exponent `e` or `E` with its own sign, and `inf` or `nan` in any case. `"infinity"` reads as `inf` with the rest left unread. An exponent marker with no digits is not part of the number: `"1e"` reads as 1.

`FloatParser<T>` has the same calls as the integer parser (`Push`, `Pop`, `Finish`), and `IsExponentPending()`, which is true after an exponent marker with no digit yet.

---

## Errors

`NumberErrorCode` values:

| Code | Meaning | Data |
|---|---|---|
| `None` | no error | |
| `Empty` | no digit was read | |
| `InvalidCharacter` | a character the grammar does not take here | `position` is its index |
| `SignNotAllowed` | a `+` not allowed, or a `-` on an unsigned type or with the sign turned off | `position` |
| `OutOfRange` | the value is outside the range | `minimum` and `maximum` of the range |
| `PrefixWithoutDigits` | `0x` or `0b` with nothing after it | |
| `ExponentWithoutDigits` | `e` with no digit, reported by `Finish()` of a streaming float parser | |

`NumberError<T>` carries the code, the `position` (the index of the character that caused it, or the count of accepted characters when the input ended), and the range. `NumberResult<T>` carries `value`, `error` and `endPosition`.

For a whole string that has no digits, the error is the one for the character that stopped it: `"x"` gives `InvalidCharacter`, and `"-5"` for an unsigned type gives `SignNotAllowed`.

---

## Formatting

Every formatter writes into a `std::span<char>`, returns the number of characters written, and never allocates. When the text does not fit, it writes nothing and returns 0. The text is not terminated; `std::string_view(buffer, length)` is the result.

- `FormatInteger<T>(output, value, NumberBase)`: bases 2, 8, 10 and 16. A negative value in base 2, 8 or 16 shows the two's complement at the width of its type: `int8_t` -1 is `FF`.
- `FormatFloat<T>(output, value, precision)`: fixed point with up to 40 digits after the point, and scientific form from 2^64 up. `NaN`, `Inf` and `-Inf` are written as text.
- `FormatFixedPoint(output, value, precision)`: fixed point with no exponent, precision clamped to 0..9, and no minus sign on a result that rounds to zero. It returns 0 for NaN, infinity, or a magnitude that would reach 9.0e18 once scaled.

The integer and float formatters match the text `Print::NumberToString` and `Print::FloatToString` produce. The fixed-point formatter matches `JSON::Writer::Number`.

---

## Accuracy

Two parsers, two guarantees:

- **Whole string** (`ParseFloat<T>`, `Print::StringToFloat`, `JSON`): correctly rounded (round half to even) for any number of significant digits, for `float` and `double`. A text with more than 19 digits is rounded from its first 19 digits and the next larger mantissa; when those two candidates round to different floats, the whole text is compared exactly with the midpoint between them, using a base-10^9 integer of at most 86 limbs. No heap is used. With GCC 15.2 the stack depth of `ParseFloat<double>` on that path is 752 bytes at `-O2` (a 320-byte caller frame and 432 bytes in `CompareWithMidpoint`) and 784 bytes at `-Os`, measured with `-fstack-usage`; other compilers and inlining choices give other figures.
- **Streaming** (`FloatParser<T>`): it keeps 19 significant digits and does not keep the text. A mantissa of at most 19 digits gives the correctly rounded value. Beyond 19 digits the value is within 2 ulp of the correctly rounded one, and the largest distance seen in the tests is 1 ulp.

Both use the same exact conversion of a mantissa. The fast path (a mantissa that is exact in the type, at most 2^53 for `double` and 2^24 for `float`, with a power of ten within ±22 for `double` and ±10 for `float`) is one exact operation; other inputs go through an exact big-integer conversion.

- The tests compare with the host's `strtod` and `strtof`: the whole-string results must be equal, and the streaming results are checked against the 2 ulp bound.

---

## Compile-Time Use

Integer parsing and integer formatting are `constexpr`, and so are float parsing and float formatting (`FormatFloat` uses `std::bit_cast` where a libm function would otherwise be needed):

```cpp
static_assert(ParseInteger<uint8_t>("255").value == 255);
static_assert(ParseFloat<double>("0.1").value == 0.1);
```

---

## Tests

Host tests in `Common/Utilities/TextNumber/.tool/sim`, run with `run.sh`, which compiles each `Test*.cpp` with g++ and `-Werror`:

- `TestIntegerParser.cpp`: the limits of every integer type, bases and prefixes, sign rules, ranges, each error, `Pop` at every position, rejected characters, a cross-check against `strtoll` and `strtoull`.
- `TestFloatParser.cpp`: a table and random texts against `strtod` and `strtof`, inf and nan, exponents, many digits, halfway points (exact decimals of midpoints between neighbouring floats, with and without a non-zero digit far out), random texts of 1 to 800 digits, `Pop` at every position, compile-time cases.
- `TestFormat.cpp`: the golden cases (`GoldenBody.inc`), round trips, buffers that are too small, compile-time text.
- `TestPrintJsonGolden.cpp`: the same golden cases through `Print` and `JSON`, so any change in their output shows here.
