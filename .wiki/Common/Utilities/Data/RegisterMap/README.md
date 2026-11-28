# RegisterMap

Typed register descriptors for I2C, SPI and Modbus. A descriptor is a constant with the register's address, value type,
byte order and access rule. It owns no storage, so an unused register costs nothing in flash or RAM. Drivers read and write
through the free templates in `RegisterAccess.h`, and a Modbus server serves user variables through a `RegisterView`.

Every mistake the compiler can see is a compile error with a message: a write to a read-only register, a bit field outside
its register, two registers at one address, a view block past Modbus address 0xFFFF.

## Headers

| Header | What it gives | Page |
|---|---|---|
| `RegisterDescriptor.h` | `RegisterDescriptor<T>`, the access enum. The byte order is `ByteOrder` of `ByteConverter.h` | [RegisterDescriptor](RegisterDescriptor) |
| `BitField.h` | `BitField<Width>(register, shift)`, a part of a register | [BitField](BitField) |
| `RegisterAccess.h` | `Read`, `Write`, `Modify` over a bus | [RegisterAccess](RegisterAccess) |
| `RegisterSet.h` | `RegisterSet<...>`, a list of registers with the duplicate check | [RegisterSet](RegisterSet) |
| `RegisterView.h` | `RegisterViewBuilder`, `Variable`, `Computed`, `RegisterView<Make>`: the entries a Modbus server serves | [RegisterView](RegisterView) |
| `RegisterBus.h` | `RegisterTable`, `RegisterRef`, the `RegisterBus` concept a bus implements | [RegisterBus](RegisterBus) |

The bus implementations are in `Common/Drivers/Interface/RegisterBus/`: `I2CRegisterBus.h` and `SpiRegisterBus.h`
([register bus pages](../../../Drivers/Interface/RegisterBus/README)).

## Example

```cpp
#include <Utilities/Data/RegisterMap/BitField.h>
#include <Utilities/Data/RegisterMap/RegisterAccess.h>
#include <Drivers/Interface/RegisterBus/I2CRegisterBus.h>

// A charger over I2C at 0x09. The register is one byte; bit 3 disables charging.
constexpr auto ChargeControl = RegisterDescriptor<uint8>(0x09, 0x01).ReadWrite();
constexpr auto ChargeDisable = BitField<1>(ChargeControl, 3);
constexpr auto Measured = RegisterDescriptor<uint16>(0x68, 0x3B).ReadOnly();

I2CRegisterBus<I2CAdapterG4> bus(i2c);

Result<uint8> value = Read<ChargeControl>(bus);   // one read
Write<ChargeControl>(bus, 0x00);                  // one write
Modify<ChargeDisable>(bus, true);                 // one read, bit 3 set, one write
Result<uint16> adc = Read<Measured>(bus);         // big-endian by default
```

## Compile-time rules

Each row is a compile error. The message is the one the compiler prints.

| Rule | Message |
|---|---|
| Write to a read-only register | `register is read-only: Write<Reg>() needs Write or ReadWrite access` |
| Read from a write-only register | `register is write-only: Read<Reg>() needs Read or ReadWrite access` |
| Modify on a register without read and write | `Modify<Field>() needs ReadWrite access` |
| Bit field outside its register | `bit field is outside the register width` |
| Device address above 7 bits | `device address must be 7 bits` |
| Address width other than 1 or 2 bytes | `address width must be 1 or 2 bytes` |
| Two registers at one table, device and address (in a `RegisterSet`) | `two registers share a table, device and address` |
| Two view entries of one table at one address | `two view entries overlap in the same table at one Modbus address` |
| A view block past Modbus address 0xFFFF | `a block runs past Modbus address 0xFFFF` |
| A holding or input value of the wrong size | `a register value is an integer of 2, 4 or 8 bytes, or an array of 16-bit integers` |
| A coil or discrete value that is not bool | `a coil or discrete input is a bool or an array of bool` |
| A set function on an input or discrete entry | `input registers and discrete inputs cannot take a set function` |

## Checks run when the view is instantiated

A `RegisterView` is checked when the class is instantiated, not when an alias names it. Use the view, or name one of its
static members, so the checks run:

```cpp
using Table = RegisterView<+[] { return RegisterViewBuilder().HoldingRegisters(0x0000, Variable(&setpoint)); }>;

static_assert(Table::checked);   // runs the overlap check; the alias alone does not
```

## Sizes and time

- No heap, no `std::function`, no virtual function, no exceptions, no RTTI.
- A descriptor, a bit field and a view entry are constants in flash. A register that nothing names leaves no symbol at `-Os`.
- Each `Read`, `Write` and `Modify` is one bus transaction, or two for `Modify`.
- Each function template is instantiated only for the descriptor it is given.
