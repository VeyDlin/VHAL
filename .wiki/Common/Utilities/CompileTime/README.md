# CompileTime

The compile-time checks of the library are in `Common/Utilities/CompileTime/`. `Require.h` holds the checks, and `TextLiteral.h` holds the string literal type that carries their messages. `TypeList.h` holds the searches over a list of types, described on the [TypeList](TypeList.md) page.

| Function | Checks | Diagnostic |
|---|---|---|
| `Require<"Module: message">(condition)` | a condition | the message |
| `RequireValue<"Module: message", value>(condition)` | a condition, with a value to show | the message and the value |

A check is a `constexpr void` function. When its condition is false it calls a function that is declared and never defined. A constant expression that reaches that call does not compile, and the compiler prints the message, which is a template argument. A call that runs at run time has no definition to link to, so a builder that is not evaluated at compile time fails at link time.

```cpp
#include <Utilities/CompileTime/Require.h>

constexpr int CheckedSum(int first, int second) {
	Require<"CompileTime: the sum is not negative">(first + second >= 0);
	return first + second;
}

constexpr int result = CheckedSum(-4, 1);
```

`RequireValue` takes the value as a second template argument, of type `std::size_t`. Use it when the message needs the value that failed:

```cpp
constexpr int CheckedValue(int value) {
	RequireValue<"CompileTime: the first value is below 10", 10>(value < 10);
	return value;
}
```

## Messages

Every message starts with the name of the module that checks it, then a colon and a space, then the message:

```text
AT: a command name is empty
Modbus: timeout must be above 0 ms
```

The module name opens the message, so the text names the library that reports the failure. A `RequireValue` message does not write the value into its text: the value is a separate template argument, and the diagnostic prints it.

`TextLiteral<Size>` is the type of a message. It is a class with a `char` array and a constructor from a string literal of the same size. `GetLength()` gives the length without the terminating NUL, and `View()` gives a `std::string_view` of the text.

## The GCC diagnostic

The failing snippet below is `Common/Utilities/CompileTime/.test/BuilderErrors/require_fails.cpp`, compiled with g++ 15.2 and `-std=c++23`:

```cpp
constexpr int CheckedSum(int first, int second) {
	Require<"CompileTime: the sum is not negative">(first + second >= 0);
	return first + second;
}

constexpr int result = CheckedSum(-4, 1);
```

GCC prints the call chain and then the error. The error line that carries the message is:

```text
Common/Utilities/CompileTime/Require.h:23:42: error: call to non-'constexpr' function 'void CompileTimeError() [with TextLiteral<...auto...> Message = TextLiteral<37>{"CompileTime: the sum is not negative"}]'
```

The `in 'constexpr' expansion of 'Require<...>'` line above it names the check that failed. For `RequireValue` the error line carries the value after the message, as `Value = 10` in the template arguments:

```text
Common/Utilities/CompileTime/Require.h:37:54: error: call to non-'constexpr' function 'void CompileTimeErrorValue() [with TextLiteral<...auto...> Message = TextLiteral<41>{"CompileTime: the first value is below 10"}; long long unsigned int Value = 10]'
```

## Host definitions

A host program that runs a builder at run time needs definitions of `CompileTimeError` and `CompileTimeErrorValue`, so the calls link. `Common/Utilities/CompileTime/.test/CompileTimeError.h` provides them. Each prints its message to `stderr`, with the value for `RequireValue`, and calls `std::abort()`. The definitions are not `constexpr`, so a constant expression that reaches them still does not compile, as on the target. Only host tests include this header. A target build does not.

## Module prefixes

The prefixes in use in the tree, from the `Require<"...">` and `RequireValue<"...">` calls:

| Prefix | Module |
|---|---|
| `AT` | `Utilities/AT` |
| `CRC` | `Utilities/Checksum/CRC` |
| `CommandLine` | `Utilities/CommandLine` |
| `Fat12` | `Utilities/FileSystem` |
| `Modbus` | `Drivers/Interface/Modbus` |
| `Regex` | `Utilities/Regex` |
| `RegisterBus` | `Drivers/Interface/RegisterBus` |
| `RegisterMap` | `Utilities/Data/RegisterMap` |
| `RTU` | `Drivers/Interface/Modbus/RTU` |
| `USB` | `Drivers/Interface/USB` |
| `VUB` | `Drivers/Interface/VUB` |

The host tests of this module use the prefix `CompileTime`. A module that adds a check takes its own prefix, in the same form.
