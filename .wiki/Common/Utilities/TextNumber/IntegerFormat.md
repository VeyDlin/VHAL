# IntegerFormat

`Common/Utilities/TextNumber/IntegerFormat.h`: writes an integer of up to 64 bits as text, in base 2, 8, 10 or 16.

---

## Use

```cpp
char buffer[66] = {};
size_t length = FormatInteger<int8_t>(std::span<char>(buffer), static_cast<int8_t>(-1), NumberBase::Hexadecimal);
// length 2, buffer "FF"

size_t binaryLength = FormatInteger<uint8_t>(std::span<char>(buffer), 5, NumberBase::Binary);
// "00000101": binary always shows every bit of the type
```

`NumberBase` is `Binary` (2), `Octal` (8), `Decimal` (10) or `Hexadecimal` (16).

---

## Rules

These match `Print::NumberToString`:

- Decimal shows a minus sign for a negative value and no leading zeros; zero is `0`.
- Hexadecimal and octal show no leading zeros, except that zero is `0`.
- A negative value in any base other than decimal shows the two's complement at the width of its type: `int8_t` -1 is `FF`, `int16_t` -1 is `FFFF`, and `int64_t` -1 is sixteen `F`s.
- Binary shows every bit of the type, leading zeros included.

---

## Buffers

The function writes the whole text or nothing. When `output` is too small it returns 0 and leaves the buffer unchanged. It does not add a terminator. `Print::NumberToString` truncates and terminates instead; that difference belongs to whoever calls it.

---

## Compile Time

The whole function is `constexpr`, so a constexpr helper can check the text at compile time:

```cpp
constexpr bool HexIsFF() {
    char buffer[8] = {};
    size_t length = FormatInteger<int8_t>(std::span<char>(buffer), static_cast<int8_t>(-1), NumberBase::Hexadecimal);
    return std::string_view(buffer, length) == "FF";
}
static_assert(HexIsFF());
```
