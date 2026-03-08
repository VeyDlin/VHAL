# DataTypes

Core type aliases and compiler attribute macros used across the entire VHAL codebase.

## Integer Types

Fixed-width integer aliases for cleaner code:

| Alias | Underlying Type | Size |
|-------|----------------|------|
| `int8` | `int8_t` | 1 byte |
| `int16` | `int16_t` | 2 bytes |
| `int32` | `int32_t` | 4 bytes |
| `int64` | `int64_t` | 8 bytes |
| `uint8` | `uint8_t` | 1 byte |
| `uint16` | `uint16_t` | 2 bytes |
| `uint32` | `uint32_t` | 4 bytes |
| `uint64` | `uint64_t` | 8 bytes |

## Compiler Attributes

### Function Attributes

| Macro | Effect |
|-------|--------|
| `_AInline` | Force inline (`always_inline`) |
| `_ANoinline` | Prevent inlining |
| `_ANoreturn` | Mark function as non-returning |
| `_AUsed` | Prevent linker from discarding the symbol |
| `_AWeak` | Weak linkage -- can be overridden |

### Packing Attributes

| Macro | Effect |
|-------|--------|
| `_APacked` | Remove struct padding |
| `_APacked1` | Packed, aligned to 1 byte |
| `_APacked2` | Packed, aligned to 2 bytes |
| `_APacked4` | Packed, aligned to 4 bytes |
| `_APackedAligned(x)` | Packed with custom alignment |

### Debug

| Macro | Effect |
|-------|--------|
| `__FILENAME_ONLY__` | Extracts filename without path from `__FILE__` |

## Usage

```cpp
#include <Utilities/DataTypes.h>

uint32 counter = 0;
int16 temperature = -25;

struct _APacked SensorData {
    uint8 id;
    uint16 value;
    int32 timestamp;
};

_AInline void FastOperation() {
    // always inlined
}
```

This header is included transitively via `VHAL.h` -- direct include is rarely needed.
