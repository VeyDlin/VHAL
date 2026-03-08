# JSON

Lightweight JSON serializer and parser for embedded systems with zero dynamic memory allocation.

## Table of Contents

- [Quick Start](#quick-start)
- [Writer](#writer)
  - [Basic Object](#basic-object)
  - [Nested Objects and Arrays](#nested-objects-and-arrays)
  - [Number Formatting](#number-formatting)
  - [Error Handling](#error-handling)
- [Parser](#parser)
  - [Parsing Strings](#parsing-strings)
  - [Parsing Numbers](#parsing-numbers)
  - [Parsing Booleans and Null](#parsing-booleans-and-null)
  - [Value Type Detection](#value-type-detection)
  - [Skipping Values](#skipping-values)
- [Static Helpers](#static-helpers)
  - [GetString](#getstring)
  - [GetNumber](#getnumber)
- [API Reference](#api-reference)

## Quick Start

**Writing JSON:**

```cpp
#include <JSON/JSON.h>

std::array<char, 256> buffer{};
JSON::Writer writer(std::span(buffer));

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
const char* json = R"({"temperature":25.5,"unit":"C"})";

double temp;
JSON::GetNumber(json, "temperature", temp);  // temp = 25.5

std::array<char, 16> unitBuf{};
size_t unitLen;
JSON::GetString(json, "unit", std::span(unitBuf), unitLen);  // unitBuf = "C"
```

## Writer

`JSON::Writer` streams JSON output into a fixed-size `std::span<char>` buffer. All methods return `Writer&`, enabling a fluent chaining API.

### Basic Object

```cpp
std::array<char, 128> buffer{};
JSON::Writer w(std::span(buffer));

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

### Nested Objects and Arrays

```cpp
std::array<char, 512> buffer{};
JSON::Writer w(std::span(buffer));

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

### Number Formatting

```cpp
JSON::Writer w(std::span(buffer));

w.StartObject()
 .Key("int_val").Number(static_cast<int64>(-42))
 .Comma()
 .Key("uint_val").Number(static_cast<uint64>(100))
 .Comma()
 .Key("float_default").Number(3.14159)          // 6 decimal places (default)
 .Comma()
 .Key("float_precise").Number(3.14159, 2)       // 2 decimal places
 .EndObject();
```

### Error Handling

If the buffer overflows, writing stops and `HasError()` returns `true`.

```cpp
std::array<char, 10> smallBuffer{};
JSON::Writer w(std::span(smallBuffer));

w.StartObject()
 .KeyValue("long_key_name", "long_value")
 .EndObject();

if (w.HasError()) {
    // Buffer was too small
}
```

## Parser

`JSON::Parser` provides sequential token-based parsing of a JSON string. It does not allocate memory -- parsed strings are written into caller-provided `std::span<char>` buffers.

### Parsing Strings

```cpp
const char* json = R"("hello world")";
JSON::Parser parser(json);

std::array<char, 64> buf{};
size_t len;
if (parser.ParseString(std::span(buf), len)) {
    // buf contains "hello world", len = 11
}
```

Escape sequences (`\"`, `\\`, `\n`, `\t`, etc.) are decoded automatically.

### Parsing Numbers

```cpp
const char* json = "42";
JSON::Parser parser(json);

int64 intVal;
parser.ParseNumber(intVal);  // intVal = 42

// Also supports uint64 and double overloads
```

### Parsing Booleans and Null

```cpp
JSON::Parser pTrue("true");
bool val;
pTrue.ParseBoolean(val);  // val = true

JSON::Parser pNull("null");
pNull.ParseNull();  // returns true
```

### Value Type Detection

```cpp
JSON::Parser parser(R"({"key": "value"})");
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

`SkipValue()` advances past the current value (string, object, array, or primitive), correctly handling nested structures and escaped strings.

```cpp
// Useful when iterating object keys and ignoring unwanted ones
parser.SkipValue();
```

### FindValue

`FindValue` searches a top-level JSON object for a key and returns the raw value as a `std::string_view`. The value is written into the provided buffer.

```cpp
const char* json = R"({"status":"ok","code":200})";
JSON::Parser parser(json);

std::array<char, 64> valueBuf{};
std::string_view val = parser.FindValue("status", std::span(valueBuf));
// val == "ok"

val = parser.FindValue("code", std::span(valueBuf));
// val == "200"
```

## Static Helpers

Convenience functions for one-shot key lookups on a JSON object string. No need to create a `Parser` instance manually.

### GetString

```cpp
const char* json = R"({"device":"sensor01","location":"room1"})";

std::array<char, 32> deviceBuf{};
size_t deviceLen;
bool found = JSON::GetString(json, "device", std::span(deviceBuf), deviceLen);
// found = true, deviceBuf = "sensor01", deviceLen = 8
```

### GetNumber

Overloads for `int64`, `uint64`, and `double`:

```cpp
const char* json = R"({"temp":25.5,"count":100,"offset":-10})";

double temp;
JSON::GetNumber(json, "temp", temp);       // temp = 25.5

uint64 count;
JSON::GetNumber(json, "count", count);     // count = 100

int64 offset;
JSON::GetNumber(json, "offset", offset);   // offset = -10
```

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
| `Key(std::string_view key)` | Write `"key":` |
| `String(std::string_view value)` | Write `"value"` with escape handling |
| `Number(int64 value)` | Write signed integer |
| `Number(uint64 value)` | Write unsigned integer |
| `Number(double value, int precision = 6)` | Write floating-point number |
| `Boolean(bool value)` | Write `true` or `false` |
| `Null()` | Write `null` |
| `Comma()` | Write `,` |
| `KeyValue(key, value)` | Shorthand for `Key(key).String/Number/Boolean(value)` |
| `GetLength()` | Number of characters written |
| `HasError()` | `true` if buffer overflowed |
| `GetResult()` | `std::string_view` of written content |

### `JSON::Parser`

| Method | Description |
|--------|-------------|
| `Parser(std::string_view jsonText)` | Construct with JSON input |
| `ParseString(std::span<char> output, size_t& length)` | Parse a quoted string into buffer |
| `ParseNumber(int64& value)` | Parse integer |
| `ParseNumber(uint64& value)` | Parse unsigned integer |
| `ParseNumber(double& value)` | Parse floating-point number |
| `ParseBoolean(bool& value)` | Parse `true`/`false` |
| `ParseNull()` | Parse `null` |
| `FindValue(std::string_view key, std::span<char> valueBuffer)` | Look up a key in a top-level object, return value as `std::string_view` |
| `SkipValue()` | Skip past the current value |
| `GetValueType()` | Peek at next value and return its `JSON::Type` |
| `IsAtEnd()` | `true` if no more content to parse |

### Static Helpers

| Function | Description |
|----------|-------------|
| `JSON::GetString(json, key, output, length)` | Look up a string value by key |
| `JSON::GetNumber(json, key, int64&)` | Look up a signed integer by key |
| `JSON::GetNumber(json, key, uint64&)` | Look up an unsigned integer by key |
| `JSON::GetNumber(json, key, double&)` | Look up a floating-point number by key |
