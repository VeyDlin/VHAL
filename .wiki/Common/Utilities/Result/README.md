# Result — Error-or-Value Type

Header-only `Result<T, E>` type for returning either a value or an error. Supports implicit construction — just return a value or an error from a function, and it wraps automatically.

Includes `ResultStatus.h` — a ready-made `ResultStatus` enum for embedded use.

## Quick Start

```cpp
#include <Utilities/Result/Result.h>

Result<int> ReadSensor() {
    int value = sensor.Read();
    if (value < 0) return ResultStatus::readError;   // implicit Err
    return value;                                     // implicit Ok
}

auto r = ReadSensor();
if (r) {
    Use(r.Value());
}
```

## Implicit Construction

`Result<T>` uses `ResultStatus` as the default error type. When `T` and `E` are different types, return values convert automatically:

```cpp
Result<int> Divide(int a, int b) {
    if (b == 0) return ResultStatus::invalidParameter;  // → Err
    return a / b;                                        // → Ok
}
```

When `T` and `E` are the same type (rare), use explicit wrappers:

```cpp
Result<int, int> Foo() {
    return Ok<int>(42);    // explicit Ok
    return Err<int>(-1);   // explicit Err
}
```

## Capture

Wrap a status code + pre-filled value into a `Result`. Useful when data is filled via pointer (e.g. hardware reads):

```cpp
template <typename DataType>
Result<DataType> Read() {
    DataType data;
    return Result<DataType>::Capture(
        ReadByteArray(reinterpret_cast<uint8*>(&data), sizeof(DataType)), data
    );
}
```

`Capture` checks if the status equals `E{}` (which is `ResultStatus::ok` for enums). If ok — returns the value, otherwise — returns the error.

## Observers

```cpp
auto r = Divide(10, 2);

r.IsOk();          // true
r.IsErr();         // false
if (r) { ... }     // operator bool — true if Ok

r.Value();         // 5 — access value (undefined if Err)
r.Error();         // access error (undefined if Ok)

*r;                // same as Value()
r->field;          // access value member

r.ValueOr(0);      // value if Ok, otherwise default
```

## Transforms

### Map

Transform the value, keep the error type:

```cpp
auto doubled = r.Map([](int v) { return v * 2; });
// Ok(5) → Ok(10)
// Err(e) → Err(e)
```

### MapErr

Transform the error, keep the value type:

```cpp
auto mapped = r.MapErr([](ResultStatus e) {
    return ErrorToString(e);
});
// Ok(5) → Ok(5)
// Err(readError) → Err("read error")
```

### AndThen

Chain operations that return Result (flatMap):

```cpp
auto result = ReadSensor().AndThen([](int v) -> Result<float> {
    if (v > 1000) return ResultStatus::outOfRange;
    return static_cast<float>(v) / 100.0f;
});
// Ok(500) → Ok(5.0f)
// Ok(2000) → Err(outOfRange)
// Err(e) → Err(e)
```

### OrElse

Recover from errors:

```cpp
auto result = ReadSensor().OrElse([](ResultStatus e) -> Result<int> {
    if (e == ResultStatus::timeout) return 0;    // fallback value
    return e;                                    // propagate other errors
});
```

## Custom Error Types

`Result<T, E>` works with any error type, not just `ResultStatus`:

```cpp
enum class ParseError { badFormat, overflow };

Result<int, ParseError> ParseInt(const char* str) {
    if (!str) return ParseError::badFormat;
    return 42;
}
```

## ResultStatus

`ResultStatus` is the default error type for `Result<T>`. Covers common embedded scenarios:

```cpp
ResultStatus::ok                  // Operation completed successfully
ResultStatus::error               // General error
ResultStatus::timeout             // Timeout exceeded
ResultStatus::busy                // Device is busy
ResultStatus::readError           // Data read error
ResultStatus::writeError          // Data write error
ResultStatus::invalidParameter    // Invalid parameter
ResultStatus::outOfMemory         // Out of memory
ResultStatus::permissionDenied    // Permission denied
// ... and many more (see ResultStatus.h for full list)
```
