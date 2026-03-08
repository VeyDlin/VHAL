# Bit — Bit-Level Operations

Bit-level operations on integer types with bounds checking.

```cpp
#include <Utilities/Data/Bit.h>

uint8 reg = 0;

Bit::Set(reg, 3);              // set bit 3:       0000 1000
Bit::Clear(reg, 3);            // clear bit 3:     0000 0000
Bit::Toggle(reg, 3);           // toggle bit 3:    0000 1000
bool is = Bit::Check(reg, 3);  // check bit 3:     true
Bit::Write(reg, 3, false);     // write bit 3 = 0: 0000 0000

// Set multiple bits at once
auto mask = Bit::ByteSet<uint8>(0, 2, 4);  // 0001 0101 (bits 0, 2, 4)

// Set/clear a bit pattern at a position
Bit::SetAt(reg, 0b11, 4);     // set 2-bit pattern at position 4
Bit::ClearAt(reg, 0b11, 4);   // clear 2-bit pattern at position 4

// Set a single bit value (clears all other bits)
Bit::SetValue(reg, 3);        // 0000 1000
```
