# Console

A lightweight, stream-style console I/O library for embedded systems (VHAL). Provides formatted text output with `<<` operator support, number formatting, hex dumps, timestamps, and structured logging.

## Table of Contents

- [Quick Start](#quick-start)
- [Print (Base Class)](#print-base-class)
  - [Write / WriteLine](#write--writeline)
  - [Number Formatting](#number-formatting)
  - [Float Formatting](#float-formatting)
  - [Buffer / Hex Dump](#buffer--hex-dump)
  - [Static Conversion Utilities](#static-conversion-utilities)
- [Console (Main Class)](#console-main-class)
  - [Setup](#setup)
  - [Stream Operator (<<)](#stream-operator-)
  - [Manipulators](#manipulators)
  - [Logging](#logging)
  - [Reading Input](#reading-input)
- [API Reference](#api-reference)

## Quick Start

```cpp
#include <Utilities/Console/Console.h>

Console console;

// Attach a write handler (e.g. UART transmit)
console.SetWriteHandler([](const char* data, size_t size) {
    UART_Transmit(data, size);
});

// Stream-style output
console << "Hello, world!" << Console::endl;
console << "Value: " << 42 << Console::endl;

// Logging with automatic timestamps
console.Info("System started");
console.Error("Sensor timeout");
```

## Print (Base Class)

`Print` is an abstract base class that provides all text output and number conversion functionality. Subclasses must implement `WriteRaw(const char* data, size_t size)`.

### Write / WriteLine

```cpp
// Strings
console.Write("Hello");              // no newline
console.WriteLine("Hello");          // appends \r\n

// string_view
console.Write(std::string_view("data", 4));

// Booleans
console.Write(true);                 // outputs "true"
console.WriteLine(false);            // outputs "false\r\n"

// Raw bytes
uint8_t buf[] = {0x41, 0x42};
console.Write(buf, 2);               // outputs raw bytes "AB"

// Newline only
console.Line();                      // outputs \r\n
console.Line("\n");                   // custom newline
```

### Number Formatting

Integers can be printed in decimal (default), hexadecimal, octal, or binary:

```cpp
console.Write(255);                            // "255"
console.Write(255, Print::Format::Hex);        // "FF"
console.Write(255, Print::Format::Bin);        // "0000000011111111" (full width for type)
console.Write(255, Print::Format::Oct);        // "377"
console.WriteLine(-42);                        // "-42\r\n"
```

### Float Formatting

```cpp
console.Write(3.14159f);                       // "3.14" (default precision 2)
console.Write(3.14159f, 4);                    // "3.1415"
console.WriteLine(0.5);                        // "0.50\r\n"
```

### Buffer / Hex Dump

`WriteBuffer` prints each byte as a two-character hex value separated by spaces:

```cpp
uint8_t data[] = {0x0A, 0xFF, 0x03};
console.WriteBuffer(data, 3);                  // "0A FF 03 "
console.WriteBufferLine(data, 3);              // "0A FF 03 \r\n"
```

### Static Conversion Utilities

These can be used without a Console instance:

```cpp
// Number to string
char buf[32];
size_t len = Print::NumberToString(buf, sizeof(buf), 255, Print::Format::Hex);  // buf = "FF"

// Float to string
len = Print::FloatToString(buf, sizeof(buf), 3.14, 3);  // buf = "3.140"

// String to number
int32_t val;
bool ok = Print::StringToNumber("123", val);             // val = 123, ok = true

// String to float
double fval;
ok = Print::StringToFloat("3.14e2", fval);               // fval = 314.0, ok = true
```

## Console (Main Class)

`Console` extends `Print` and adds stream operators, manipulators, logging, and input reading.

### Setup

```cpp
Console console;

// Write handler -- called for all output
console.SetWriteHandler([](const char* data, size_t size) {
    HAL_UART_Transmit(&huart1, (uint8_t*)data, size, 100);
});

// Read handler -- returns one character or EOF
console.SetReadHandler([]() -> int {
    uint8_t ch;
    if (HAL_UART_Receive(&huart1, &ch, 1, 100) == HAL_OK) {
        return ch;
    }
    return EOF;
});
```

### Stream Operator (<<)

The `<<` operator supports all common types:

```cpp
// Strings and numbers
console << "Temperature: " << 23.5f << " C" << Console::endl;

// Booleans
console << "Active: " << true << Console::endl;

// Set default integer format for subsequent integers
console << Print::Format::Hex << 255 << Console::endl;      // "FF\r\n"
console << Print::Format::Dec << 255 << Console::endl;      // "255\r\n"

// Raw byte spans
std::span<uint8_t> span(data, 3);
console << span;                                              // hex dump "0A FF 03 "
```

### Manipulators

Manipulators are factory functions that produce stream-insertable objects.

#### endl

```cpp
console << "Line 1" << Console::endl;
```

#### hex / dec / bin / oct (number format)

Force a specific format for a single number without changing the default:

```cpp
console << Console::hex(255)  << Console::endl;   // "FF"
console << Console::dec(255)  << Console::endl;   // "255"
console << Console::bin(255)  << Console::endl;   // "0000000011111111"
console << Console::oct(255)  << Console::endl;   // "377"
```

#### hex (buffer)

Print a byte buffer as hex:

```cpp
uint8_t buf[] = {0xDE, 0xAD};
console << Console::hex(buf, 2);                   // "DE AD "
```

#### timestamp

Outputs system uptime formatted as `[HH:MM:SS:MS]`:

```cpp
console << Console::timestamp;                     // "[00:01:23:456] "

// With explicit format
console << Console::timestamp(TimestampManipulator::Format::HMS);   // "[00:01:23] "
console << Console::timestamp(TimestampManipulator::Format::HMSM);  // "[00:01:23:456] "
console << Console::timestamp(TimestampManipulator::Format::Auto);  // auto-adds days/months/years
```

#### separator

Prints a horizontal separator line:

```cpp
console << Console::separator();                   // "----------------------------------------\r\n"
console << Console::separator("========");         // "========\r\n"
```

#### indent

Inserts a given number of spaces:

```cpp
console << Console::indent(4) << "indented" << Console::endl;  // "    indented\r\n"
```

#### tick

Outputs the raw system tick value:

```cpp
console << Console::tick << Console::endl;          // e.g. "1234567\r\n"
```

#### info / debug / error

Stream-style log level prefixes with timestamp:

```cpp
console << Console::info  << "Boot complete"  << Console::endl;
console << Console::debug << "x=" << 42       << Console::endl;
console << Console::error << "Fault detected" << Console::endl;
// Output:
// [INFO] [00:00:01:234] Boot complete
// [DEBUG] [00:00:01:235] x=42
// [ERROR] [00:00:01:236] Fault detected
```

### Logging

Direct logging methods that automatically prepend level and timestamp:

```cpp
console.Info("System ready");      // "[INFO] [00:00:01:000] System ready\r\n"
console.Debug("Entering loop");    // "[DEBUG] [00:00:01:001] Entering loop\r\n"
console.Error("Overtemperature");  // "[ERROR] [00:00:01:002] Overtemperature\r\n"
console.Log("[WARN]", "Low batt"); // "[WARN] [00:00:01:003] Low batt\r\n"
```

### Reading Input

```cpp
// Read raw characters into a buffer (stops at EOF)
char buf[64];
size_t n = console.Read(buf, sizeof(buf));

// Read a line (stops at \n or EOF)
n = console.ReadLine(buf, sizeof(buf));

// Read and parse a typed value
int val    = console.Read<int>();
float fval = console.Read<float>();
```

## API Reference

### Print

| Method | Description |
|--------|-------------|
| `WriteRaw(data, size)` | Pure virtual -- low-level byte output |
| `Line(newline = "\r\n")` | Write a newline sequence |
| `Write(T number, Format)` | Write an integer in the given format |
| `Write(T number, precision)` | Write a float with given decimal digits |
| `Write(string_view)` | Write a string |
| `Write(bool)` | Write `"true"` or `"false"` |
| `Write(uint8*, size)` | Write raw bytes |
| `WriteBuffer(uint8*, size)` | Write bytes as hex dump (`"0A FF "`) |
| `WriteLine(...)` | Same as `Write` variants, appends `\r\n` |
| `NumberToString(buf, size, number, fmt)` | Static: integer to string |
| `FloatToString(buf, size, number, prec)` | Static: float to string |
| `StringToNumber(str, result, endPos)` | Static: string to integer |
| `StringToFloat(str, result, endPos)` | Static: string to float |

### Print::Format

| Value | Base |
|-------|------|
| `Dec` | 10 |
| `Hex` | 16 |
| `Oct` | 8 |
| `Bin` | 2 |

### Console

| Method | Description |
|--------|-------------|
| `SetWriteHandler(fn)` | Set the output callback |
| `SetReadHandler(fn)` | Set the input callback (returns `int`, EOF on no data) |
| `Log(level, message)` | Print `level + timestamp + message` |
| `Info(message)` | Log with `[INFO]` prefix |
| `Debug(message)` | Log with `[DEBUG]` prefix |
| `Error(message)` | Log with `[ERROR]` prefix |
| `Read(buf, size)` | Read characters into buffer |
| `ReadLine(buf, size)` | Read until `\n` or EOF |
| `Read<T>()` | Read and parse a value of type `T` |
| `operator<<` | Stream insertion for strings, numbers, bools, spans, manipulators |

### Console Manipulators

| Factory | Result |
|---------|--------|
| `Console::endl` | Newline |
| `Console::hex(number)` | Number in hex |
| `Console::dec(number)` | Number in decimal |
| `Console::bin(number)` | Number in binary |
| `Console::oct(number)` | Number in octal |
| `Console::hex(data, size)` | Byte buffer hex dump |
| `Console::timestamp` | System uptime `[HH:MM:SS:MS]` |
| `Console::timestamp(fmt)` | Timestamp with explicit format |
| `Console::separator(str)` | Horizontal line separator |
| `Console::indent(n)` | Insert `n` spaces |
| `Console::tick` | Raw system tick value |
| `Console::info` | `[INFO]` prefix with timestamp |
| `Console::debug` | `[DEBUG]` prefix with timestamp |
| `Console::error` | `[ERROR]` prefix with timestamp |

### TimestampManipulator::Format

| Value | Output |
|-------|--------|
| `HMS` | `HH:MM:SS` |
| `HMSM` | `HH:MM:SS:MS` |
| `Auto` | Adds days/months/years when uptime exceeds 24h |
