# IntegerParser

`Common/Utilities/TextNumber/IntegerParser.h`: reads an integer of up to 64 bits, whole or one character at a time, with the same grammar either way.

---

## Options

`IntegerOptions<T>` is a struct of constexpr setters. Each setter returns a copy:

| Setter | Default | Effect |
|---|---|---|
| `WithHexPrefix()` | off | `0x` or `0X`, then hex digits in either case |
| `WithBinaryPrefix()` | off | `0b` or `0B`, then 0 and 1 |
| `WithPlusSign()` | off | a leading `+` |
| `WithoutMinusSign()` | minus allowed | a leading `-` is refused (it is always refused for an unsigned type) |
| `WithLeadingBlanks()` | off | the whole-string helper skips leading spaces and tabs |
| `WithRange(low, high)` | the type's range | the accepted values, inclusive |

A prefix letter is read only straight after a lone leading zero, so `10x5` reads as 10 and `00x5` reads as 0.

---

## Streaming

```cpp
IntegerParser<int16_t> parser(IntegerOptions<int16_t>{}.WithPlusSign());
NumberError<int16_t> signResult = parser.Push('-');   // accepted: a sign, first character, so code is None
parser.Push('4');
parser.Push('2');
parser.Pop();                                       // removes the 2
NumberResult<int16_t> result = parser.Finish();     // value -4
```

| Call | Returns | Notes |
|---|---|---|
| `Push(char)` | `NumberError<T>` | `code == None` when accepted. A refused character changes nothing, so the parser can keep going. |
| `Pop()` | nothing | Removes the last accepted character exactly. Pops of an empty parser are ignored. |
| `Finish()` | `NumberResult<T>` | The value, or the error. `endPosition` is the count of accepted characters. |

`Finish()` checks, in this order: a prefix with no digits, no digits at all, the range.

---

## Whole String

```cpp
NumberResult<uint8_t> r = ParseInteger<uint8_t>("12abc");      // value 12, endPosition 2
NumberResult<uint8_t> s = ParseInteger<uint8_t>("300");        // OutOfRange, endPosition 3
NumberResult<uint8_t> t = ParseInteger<uint8_t>("-5");         // SignNotAllowed, position 0
```

`ParseInteger` feeds the characters to the streaming parser until one is refused, then calls `Finish()`. When the text has no digits, the refused character gives the error, so the caller learns why nothing was read.

---

## Range Limits

With a range set, the error names it:

```cpp
NumberResult<uint8_t> v = ParseInteger<uint8_t>("101", IntegerOptions<uint8_t>{}.WithRange(0, 100));
// v.error.code == NumberErrorCode::OutOfRange, v.error.minimum == 0, v.error.maximum == 100
```

Digits past the 64-bit magnitude are counted and not stored. Any value with them is at least 2^64, so it is out of range for every type.

---

## Compile Time

```cpp
static_assert(ParseInteger<uint8_t>("0x1F", IntegerOptions<uint8_t>{}.WithHexPrefix()).value == 31);
```
