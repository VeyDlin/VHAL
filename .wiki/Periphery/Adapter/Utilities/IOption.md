# IOption

Type-safe option wrappers used throughout VHAL adapters to represent hardware register values, configuration selectors, and bitfield flags. Replaces raw `enum class` values with typed structs that carry their underlying register code.

Header: `#include <Adapter/Utilities/IOption.h>`

## IOption\<CodeType\>

A single-value option. Wraps one value of type `CodeType` (typically `uint8`, `uint16`, `uint32`).

```cpp
struct BaudRateOption : IOption<uint32> {
    using IOption::IOption;
};

struct BaudRate {
    static constexpr BaudRateOption B9600{9600};
    static constexpr BaudRateOption B115200{115200};
};
```

### API

| Member | Description |
|--------|-------------|
| `IOption()` | Default constructor (zero-initialized) |
| `IOption(CodeType value)` | Explicit construction from raw value |
| `Get()` | Returns the underlying `CodeType` value |
| `operator CodeType()` | Implicit conversion to `CodeType` |
| `operator==`, `operator!=` | Compare with another `IOption` or with a raw `CodeType` |

### Usage in Adapters

Adapters define option types and their valid values as `static constexpr` members of a struct:

```cpp
// In adapter
struct ModeOption : IOption<uint8> { using IOption::IOption; };
struct Mode {
    static constexpr ModeOption Normal{0};
    static constexpr ModeOption Fast{1};
    static constexpr ModeOption Turbo{2};
};

// In user code
adapter.SetMode(Mode::Fast);
```

The pattern provides type safety (can't accidentally pass a `BaudRateOption` where a `ModeOption` is expected) while keeping the raw register value accessible via `Get()`.

---

## IOptionDouble\<CodeType\>

An option that holds two values. Used when a hardware setting maps to two register fields.

```cpp
struct ChannelPairOption : IOptionDouble<uint8> {
    using IOptionDouble::IOptionDouble;
};

static constexpr ChannelPairOption Pair12{1, 2};
```

### API

| Member | Description |
|--------|-------------|
| `IOptionDouble()` | Default constructor |
| `IOptionDouble(CodeType v1, CodeType v2)` | Construct with two values |
| `Get<1>()` | Returns the first value |
| `Get<2>()` | Returns the second value |
| `operator==`, `operator!=` | Compare both values |

---

## IOptionFlag\<CodeType\>

A bitmask option. Supports bitwise OR to combine flags and `Has()` to check if a flag is set.

```cpp
struct ConfigOption : IOptionFlag<uint8> {
    using IOptionFlag::IOptionFlag;
};

struct Config {
    static constexpr ConfigOption None{0};
    static constexpr ConfigOption EnableDMA{1 << 0};
    static constexpr ConfigOption EnableIRQ{1 << 1};
    static constexpr ConfigOption EnableBoth{EnableDMA | EnableIRQ};
};
```

### API

| Member | Description |
|--------|-------------|
| `IOptionFlag()` | Default constructor (zero) |
| `IOptionFlag(CodeType value)` | Construct from raw bitmask |
| `Get()` | Returns the underlying bitmask |
| `operator CodeType()` | Implicit conversion |
| `operator\|`, `operator\|=` | Combine flags |
| `operator&` | Mask flags |
| `Has(IOptionFlag flag)` | Check if all bits in `flag` are set |
| `operator==`, `operator!=` | Exact comparison |

### Usage

```cpp
auto cfg = Config::EnableDMA | Config::EnableIRQ;

if (cfg.Has(Config::EnableDMA)) {
    // DMA is enabled
}
```
