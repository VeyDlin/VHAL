# JSON

Lightweight JSON serializer and parser for embedded systems with zero dynamic memory allocation.

## Table of Contents

- [Quick Start](#quick-start)
- [Writer](#writer)
  - [Basic Object](#basic-object)
  - [Nested Objects and Arrays](#nested-objects-and-arrays)
  - [Commas](#commas)
  - [Strings and Escaping](#strings-and-escaping)
  - [Number Formatting](#number-formatting)
  - [Error Handling](#error-handling)
- [Parser](#parser)
  - [Parsing Strings](#parsing-strings)
  - [Parsing Numbers](#parsing-numbers)
  - [Parsing Booleans and Null](#parsing-booleans-and-null)
  - [Value Type Detection](#value-type-detection)
  - [Skipping Values](#skipping-values)
  - [FindValue](#findvalue)
- [Static Helpers](#static-helpers)
  - [GetString](#getstring)
  - [GetNumber](#getnumber)
  - [GetBool](#getbool)
  - [Nested Access: GetRaw and GetArrayElement](#nested-access-getraw-and-getarrayelement)
- [Validation Scope](#validation-scope)
- [API Reference](#api-reference)

## Quick Start

**Writing JSON:**

```cpp
#include <Utilities/Serialization/JSON/JSON.h>

std::array<char, 256> buffer{};
JSON::Writer writer{std::span(buffer)};

writer.StartObject()
      .KeyValue("name", "John")
      .Comma()
      .KeyValue("age", static_cast<int64>(30))
      .Comma()
      .KeyValue("active", true)
      .EndObject();

// writer.GetResult() -> {"name":"John","age":30,"active":true}
```

**Reading JSON:**

```cpp
std::string_view json = R"({"temperature":25.5,"unit":"C"})";

double temp = 0.0;
JSON::GetNumber(json, "temperature", temp);  // temp = 25.5

std::array<char, 16> unitBuf{};
size_t unitLen = 0;
JSON::GetString(json, "unit", std::span(unitBuf), unitLen);  // unitBuf = "C"
```

Declare the writer with braces (`JSON::Writer writer{std::span(buffer)};`). The parenthesised form `JSON::Writer writer(std::span(buffer));` is parsed as a function declaration and does not compile.

## Writer

`JSON::Writer` streams JSON output into a fixed-size `std::span<char>` buffer. All writing methods return `Writer&`, enabling a fluent chaining API. Nothing is allocated; when the buffer is full the writer sets `HasError()` and stops writing.

### Basic Object

```cpp
std::array<char, 128> buffer{};
JSON::Writer w{std::span(buffer)};

w.StartObject()
 .KeyValue("device_id", "sensor-01")
 .Comma()
 .KeyValue("sample_rate", static_cast<int64>(1000))
 .Comma()
 .KeyValue("enabled", true)
 .EndObject();

std::string_view result = w.GetResult();
size_t length = w.GetLength();
```

`KeyValue` accepts a `std::string_view` or a string literal (`const char*`) as a string, any integer type (`int`, `unsigned`, `long`, `long long`, `int8`, ... `uint64`) as a number, `double` (with an optional precision), and `bool`. A literal such as `16000` writes a number.

### Nested Objects and Arrays

```cpp
std::array<char, 512> buffer{};
JSON::Writer w{std::span(buffer)};

w.StartObject()
 .Key("user").StartObject()
     .KeyValue("id", static_cast<int64>(123))
     .Comma()
     .KeyValue("email", "john@example.com")
 .EndObject()
 .Comma()
 .Key("tags").StartArray()
     .String("dark_mode")
     .Comma()
     .String("notifications")
 .EndArray()
 .EndObject();

// {"user":{"id":123,"email":"john@example.com"},"tags":["dark_mode","notifications"]}
```

### Commas

`Comma()` writes a single `,`. The writer does not insert commas itself, so write one between every two members or elements and none before the first or after the last. This is deliberate: the JWT encoder (`Crypto/JWT/JWT.h`) decides on its own where commas go, and automatic commas would produce `,,` there.

### Strings and Escaping

`String` and `Key` write the value between quotes and escape it:

| Input byte | Output |
|------------|--------|
| `"` | `\"` |
| `\` | `\\` |
| `\b`, `\f`, `\n`, `\r`, `\t` | `\b`, `\f`, `\n`, `\r`, `\t` |
| other control character (below 0x20) | `\u00XX` (lowercase hex) |
| anything else, including UTF-8 multi-byte sequences | unchanged |

```cpp
w.String("Grüße");                   // "Grüße"
w.String("a\"b\\c\n");               // "a\"b\\c\n"
```

### Number Formatting

```cpp
JSON::Writer w{std::span(buffer)};

w.StartObject()
 .Key("int_val").Number(static_cast<int64>(-42))
 .Comma()
 .Key("uint_val").Number(static_cast<uint64>(100))
 .Comma()
 .Key("float_default").Number(3.14159)          // 6 decimal places (default)
 .Comma()
 .Key("float_precise").Number(3.14159, 2)       // 2 decimal places
 .EndObject();

// {"int_val":-42,"uint_val":100,"float_default":3.141590,"float_precise":3.14}
```

- Integers are written in decimal with no formatting. Any integer type can be passed; literals such as `16000` need no cast.
- Floating-point values are rounded to the nearest value at the requested precision (halves round away from zero). Precision is clamped to 0 to 9. The rounding is done by the TextNumber library's fixed-point formatter (`Common/Utilities/TextNumber`), with the same results as before.
- `NaN`, infinity, and values whose magnitude is at least `9.0e18 / 10^precision` cannot be written as a plain JSON number. They set `HasError()` and write nothing.

### Error Handling

If the buffer overflows, writing stops and `HasError()` returns `true`. Output is truncated back to the end of the last complete token, so the buffer never holds half a string, key or number. Nothing is written after the error, even when a later token would fit.

When `HasError()` is `true`, `GetResult()` is incomplete JSON. Check `HasError()` before using it.

```cpp
std::array<char, 10> smallBuffer{};
JSON::Writer w{std::span(smallBuffer)};

w.StartObject()
 .KeyValue("long_key_name", "long_value")
 .EndObject();

if (w.HasError()) {
    // w.GetResult() is "{": the key did not fit, so none of it was kept
}
```

## Parser

`JSON::Parser` provides sequential token-based parsing of a JSON string. It does not allocate memory -- parsed strings are written into caller-provided `std::span<char>` buffers.

### Parsing Strings

```cpp
std::string_view json = R"("hello world")";
JSON::Parser parser(json);

std::array<char, 64> buf{};
size_t len = 0;
if (parser.ParseString(std::span(buf), len)) {
    // buf contains "hello world", len = 11
}
```

Escape sequences are decoded:

- `\"`, `\\`, `\/`, `\b`, `\f`, `\n`, `\r`, `\t` as usual.
- `\uXXXX` is decoded to UTF-8. A surrogate pair (`😀`) is combined into one 4-byte character. A lone or mismatched surrogate is an error.
- Raw UTF-8 in the input passes through unchanged.

`ParseString` returns `false` (and `length` is 0) for an unknown escape, an invalid `\u` sequence, a raw control character (below 0x20) inside the string, an unterminated string, or a decoded text that does not fit into the output buffer. A value that is too long is never truncated.

### Parsing Numbers

```cpp
std::string_view json = "42";
JSON::Parser parser(json);

int64 intVal = 0;
parser.ParseNumber(intVal);  // intVal = 42

// Also supports uint64 and double overloads
```

Numbers follow the JSON grammar exactly: an optional `-`, no leading `+`, no leading zeros, digits required after `.` and after `e`/`E`. The grammar is checked here, and the TextNumber library (`Common/Utilities/TextNumber`) does the conversion: integer tokens with its integer parser, and fraction and exponent tokens with its whole-string float helper, which is correctly rounded for any token length.

- The `int64` and `uint64` overloads accept only integer tokens. `1.5`, `1e3` and out-of-range values return `false`.
- The `double` overload accepts fractions and exponents. A value that overflows the double range (`1e400`) returns `false` instead of becoming infinity.
- Token length is not limited, and the value is correctly rounded for any token length (the TextNumber library, locale-free). `0.` followed by 399 zeros and `1e400` reads as `1`.
- Subnormal values are read: `1e-320` gives the subnormal value, as `strtod` does. A value below the subnormal range (`1e-400`) reads as `0` and succeeds.
- Overflow (`1e400`, `1.8e308`) returns `false`.
- A valid integer token that is out of range for `int64` or `uint64` returns `false` for those overloads. The `double` overload accepts it.

### Parsing Booleans and Null

```cpp
std::string_view json = "true";
JSON::Parser pTrue(json);
bool val = false;
pTrue.ParseBoolean(val);  // val = true

std::string_view nullText = "null";
JSON::Parser pNull(nullText);
pNull.ParseNull();  // returns true
```

### Value Type Detection

```cpp
std::string_view json = R"({"key": "value"})";
JSON::Parser parser(json);
JSON::Type type = parser.GetValueType();
// type == JSON::Type::OBJECT
```

`GetValueType()` peeks at the next non-whitespace character and returns one of:

| Return              | Detected when next char is |
|---------------------|---------------------------|
| `Type::STRING`      | `"`                       |
| `Type::OBJECT`      | `{`                       |
| `Type::ARRAY`       | `[`                       |
| `Type::BOOLEAN`     | `t` or `f`                |
| `Type::NUMBER`      | `0`-`9` or `-`            |
| `Type::NULL_TYPE`   | `n` or unrecognized       |

### Skipping Values

`SkipValue()` advances past the current value (string, object, array, number, boolean or null), handling nested structures and escaped strings. It does not allocate, and nesting depth is not limited by the stack.

```cpp
// Useful when iterating object keys and ignoring unwanted ones
parser.SkipValue();
```

If the value is malformed or unterminated, `SkipValue()` moves the parser to the end of the input, so `IsAtEnd()` returns `true`.

### FindValue

`FindValue` searches a top-level JSON object for a key. A string value is decoded into the provided buffer; any other value is returned as raw text that views the input.

```cpp
std::string_view json = R"({"status":"ok","code":200,"flag":true})";
JSON::Parser parser(json);

std::array<char, 64> valueBuf{};
std::string_view val = parser.FindValue("status", std::span(valueBuf));
// val == "ok"

val = parser.FindValue("code", std::span(valueBuf));
// val == "200"

val = parser.FindValue("flag", std::span(valueBuf));
// val == "true"
```

An empty result means the key is missing or the value could not be read (invalid escape, or a string longer than the buffer). For a string value, an empty result is also what an empty string returns, so use the static `GetString` when the difference matters.

## Static Helpers

Convenience functions for one-shot key lookups on a JSON object string. No need to create a `Parser` instance manually. Lookups search the top-level members of the object; nested objects are not searched by key (use [Nested Access](#nested-access-getraw-and-getarrayelement) for that).

### GetString

```cpp
std::string_view json = R"({"device":"sensor01","location":"room1"})";

std::array<char, 32> deviceBuf{};
size_t deviceLen = 0;
bool found = JSON::GetString(json, "device", std::span(deviceBuf), deviceLen);
// found = true, deviceBuf = "sensor01", deviceLen = 8
```

The output buffer is the only limit: a value of any length is accepted as long as its decoded form fits. When it does not fit, `GetString` returns `false` and `deviceLen` is 0. An empty string value (`""`) succeeds with length 0.

### GetNumber

Overloads for `int64`, `uint64`, and `double`:

```cpp
std::string_view json = R"({"temp":25.5,"count":100,"offset":-10,"scale":1e5})";

double temp = 0.0;
JSON::GetNumber(json, "temp", temp);       // temp = 25.5

uint64 count = 0;
JSON::GetNumber(json, "count", count);     // count = 100

int64 offset = 0;
JSON::GetNumber(json, "offset", offset);   // offset = -10

double scale = 0.0;
JSON::GetNumber(json, "scale", scale);     // scale = 100000.0
```

`int64` and `uint64` fail for a value with a fraction or exponent (`25.5` is not an integer) and `uint64` fails for a negative value.

### GetBool

```cpp
std::string_view json = R"({"is_final":true,"speech_final":false})";

bool isFinal = false;
JSON::GetBool(json, "is_final", isFinal);       // isFinal = true
```

Returns `false` when the key is missing or the value is not the literal `true` or `false` (a string `"true"` does not count).

### Nested Access: GetRaw and GetArrayElement

Two functions reach into nested values without copying. Both return a `std::string_view` that views the input, and an empty view when nothing is found, so they chain:

- `GetRaw(json, key)` returns the raw JSON text of a top-level value: an object, array, string (with its quotes), number, boolean or null.
- `GetArrayElement(array, index)` returns the raw text of element `index` of a JSON array. It is empty when the index is out of range or the text is not a complete array.

The xAI streaming chat chunk shows the pattern. The content of the first choice is reached as `choices` -> element 0 -> `delta` -> `content`:

```cpp
// data: {"id":"94dbec57-...","object":"chat.completion.chunk","created":1794956456,
//        "model":"grok-4.20-0309-non-reasoning",
//        "choices":[{"index":0,"delta":{"content":"Grüße"}}]}
std::string_view chunk = /* one SSE data payload */;

std::string_view choice = JSON::GetArrayElement(JSON::GetRaw(chunk, "choices"), 0);
std::string_view delta = JSON::GetRaw(choice, "delta");

std::array<char, 32> text{};
size_t textLength = 0;
if (JSON::GetString(delta, "content", std::span(text), textLength)) {
    // text[0..textLength) == "Grüße" (UTF-8)
}
```

A transcript event works the same way. Top-level keys are read with the same calls, and array elements by index:

```cpp
// {"type":"transcript.partial","text":"Hello, what time is it in Tokyo?",
//  "words":[{"text":"Hello,","start":0.021,"end":0.442},{"text":"what","start":0.784,"end":1.105}],
//  "is_final":true,"speech_final":false,"start":0.001,"duration":2.549,"language":"en"}
bool speechFinal = true;
JSON::GetBool(json, "speech_final", speechFinal);  // false

std::string_view second = JSON::GetArrayElement(JSON::GetRaw(json, "words"), 1);
JSON::GetNumber(second, "end", endTime);           // 1.105
```

Top-level keys are matched only at the top level, so the `start` of the event (`0.001`) is not confused with the `start` fields inside `words`.

## Validation Scope

Lookups and `ParseString` validate what they read:

- Strings: escapes, `\u` sequences, raw control characters, termination.
- Numbers and the literals `true`, `false`, `null`: the JSON grammar, and a delimiter must follow them.
- Bracket balance of every object and array that is traversed.
- A top-level object or array passed to a lookup must be closed. A truncated object yields no members: `GetString`, `GetNumber` and `GetBool` return `false`, and `GetRaw`, `GetArrayElement` and `FindValue` return an empty view.

Not checked: the placement of `:` and `,` inside objects and arrays that are not otherwise read, and whether `}` closes a `[`. Malformed separators are rejected when they are reached during a lookup, but a document with such an error in an unrelated member is not reported as a whole.

Nesting depth is not limited by recursion: the skipping code is iterative, so deep input cannot exhaust the stack.

## API Reference

### Enum `JSON::Type`

| Value        | Meaning         |
|--------------|-----------------|
| `OBJECT`     | JSON object `{}`|
| `ARRAY`      | JSON array `[]` |
| `STRING`     | Quoted string   |
| `NUMBER`     | Numeric literal |
| `BOOLEAN`    | `true`/`false`  |
| `NULL_TYPE`  | `null`          |

### `JSON::Writer`

| Method | Description |
|--------|-------------|
| `Writer(std::span<char> outputBuffer)` | Construct with output buffer |
| `StartObject()` / `EndObject()` | Write `{` / `}` |
| `StartArray()` / `EndArray()` | Write `[` / `]` |
| `Key(std::string_view key)` | Write `"key":` (escaped) |
| `String(std::string_view value)` | Write `"value"` with escaping; UTF-8 is unchanged |
| `Number(integer)` | Write any integer type except `bool` |
| `Number(double value, int precision = 6)` | Write a rounded decimal (0 to 9 places); sets `HasError()` for NaN, infinity or an unrepresentable magnitude |
| `Boolean(bool value)` | Write `true` or `false` |
| `Null()` | Write `null` |
| `Comma()` | Write `,` |
| `KeyValue(key, string_view \| const char*)` | `Key(key).String(value)` |
| `KeyValue(key, integer)` | `Key(key).Number(value)` |
| `KeyValue(key, double, int precision = 6)` | `Key(key).Number(value, precision)` |
| `KeyValue(key, bool)` | `Key(key).Boolean(value)` |
| `GetLength()` | Number of characters written |
| `HasError()` | `true` if the buffer overflowed or a value could not be written |
| `GetResult()` | `std::string_view` of written content |

### `JSON::Parser`

| Method | Description |
|--------|-------------|
| `Parser(std::string_view jsonText)` | Construct with JSON input |
| `ParseString(std::span<char> output, size_t& length)` | Parse and decode a quoted string into buffer; fails on overflow |
| `ParseNumber(int64& value)` | Parse an integer token |
| `ParseNumber(uint64& value)` | Parse an unsigned integer token |
| `ParseNumber(double& value)` | Parse a number token; fails on overflow to infinity |
| `ParseBoolean(bool& value)` | Parse `true`/`false` |
| `ParseNull()` | Parse `null` |
| `FindValue(std::string_view key, std::span<char> valueBuffer)` | Look up a key in a top-level object; strings are decoded, other values are raw text |
| `SkipValue()` | Skip past the current value (moves to the end if malformed) |
| `GetValueType()` | Peek at next value and return its `JSON::Type` |
| `IsAtEnd()` | `true` if no more content to parse |

### Static Helpers

| Function | Description |
|----------|-------------|
| `JSON::GetString(json, key, output, length)` | Decoded string value; fails when it does not fit `output` |
| `JSON::GetNumber(json, key, int64&)` | Integer value; fails on a fraction, exponent or overflow |
| `JSON::GetNumber(json, key, uint64&)` | Unsigned integer value; fails on a fraction, exponent or negative value |
| `JSON::GetNumber(json, key, double&)` | Floating-point value; fails on overflow to infinity |
| `JSON::GetBool(json, key, bool&)` | `true` or `false` value |
| `JSON::GetRaw(json, key)` | Raw text of any top-level value (empty when not found) |
| `JSON::GetArrayElement(array, index)` | Raw text of an array element (empty when out of range) |
