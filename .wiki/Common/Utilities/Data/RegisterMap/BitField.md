# BitField

Header: `#include <Utilities/Data/RegisterMap/BitField.h>`

A part of a register, for `Modify`. The width is a template argument, because the value type depends on it: `bool` for one
bit, and otherwise the smallest unsigned type that holds the width.

```cpp
constexpr auto ChargeDisable = BitField<1>(ChargeControl, 3);   // bit 3, a bool
constexpr auto Mode = BitField<2>(ChargeControl, 5);            // bits 5 and 6, a uint8

Modify<ChargeDisable>(bus, true);
Modify<Mode>(bus, 2);
```

| Width | Value type |
|---|---|
| 1 | `bool` |
| 2 to 8 | `uint8` |
| 9 to 16 | `uint16` |
| 17 to 32 | `uint32` |
| 33 to 64 | `uint64` |

The field must lie inside its register: `shift + width` is at most the register's bit count. Otherwise the compiler prints
`bit field is outside the register width`. A width of 0 or above 64 is a `static_assert`.

`Modify` checks the value against the width at run time. A value that does not fit returns `invalidParameter`. The other bits
of the register keep their values.
