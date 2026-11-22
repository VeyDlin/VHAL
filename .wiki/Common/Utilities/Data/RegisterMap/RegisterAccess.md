# RegisterAccess

Header: `#include <Utilities/Data/RegisterMap/RegisterAccess.h>`

Free templates for drivers. The register is a template argument, so the access rule is checked at compile time and the
address comes from the constant. The bus is any type that meets the [`RegisterBus`](RegisterBus) concept.

```cpp
Result<uint8> value = Read<ChargeControl>(bus);   // one read, decoded in the register's byte order
Write<ChargeControl>(bus, 0x00);                  // one write, encoded in the register's byte order
Modify<ChargeDisable>(bus, true);                 // one read, the field's bit changed, one write
```

| Function | Returns | Rule |
|---|---|---|
| `Read<Reg>(bus)` | `Result<ValueType>` | needs `CanRead()`. A bus error is the result's error |
| `Write<Reg>(bus, value)` | `ResultStatus` | needs `CanWrite()`. The value has the register's value type |
| `Modify<Field>(bus, value)` | `ResultStatus` | needs `ReadWrite`. `Field` is a [`BitField`](BitField) |

A `Modify` value that does not fit the field returns `invalidParameter` before any bus traffic. A `Modify` whose read fails
does not write.

A literal converts to the register's value type when it fits. A value of another integer type converts implicitly; build the
tests with `-Wconversion` to see a narrowing.

The errors for a wrong access rule are compile errors:

```text
register is read-only: Write<Reg>() needs Write or ReadWrite access
register is write-only: Read<Reg>() needs Read or ReadWrite access
Modify<Field>() needs ReadWrite access
```
