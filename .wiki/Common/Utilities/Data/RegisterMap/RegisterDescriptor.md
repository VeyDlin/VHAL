# RegisterDescriptor

Header: `#include <Utilities/Data/RegisterMap/RegisterDescriptor.h>`

A register's address, value type and rules, as a `constexpr` constant. The value type is an integer type, and the descriptor
owns no storage.

```cpp
constexpr auto ChargeControl = RegisterDescriptor<uint8>(0x09, 0x01).ReadWrite();
constexpr auto Measured = RegisterDescriptor<uint16>(0x68, 0x3B)
	.AddressWidth(2)
	.Order(ByteOrder::little)
	.Table(RegisterTable::Input)
	.ReadOnly();
```

The constructor takes the device address and the register address. The device address is the 7-bit I2C address; SPI and
Modbus ignore it, and it must be 7 bits or less.

## Builder methods

Each method returns a new descriptor. The defaults are an address width of 1 byte, big-endian, read and write, and the
holding table.

| Method | Sets |
|---|---|
| `AddressWidth(1 or 2)` | the register address size on the wire, in bytes |
| `Order(ByteOrder)` | `big` (default) or `little` for the value's bytes. `ByteOrder` is declared in `Utilities/Data/ByteConverter.h` |
| `Table(RegisterTable)` | the Modbus table: `Holding`, `Input`, `Coil` or `Discrete`. I2C and SPI ignore it |
| `ReadOnly()`, `WriteOnly()`, `ReadWrite()` | the access rule |

## Queries

| Member | Result |
|---|---|
| `CanRead()`, `CanWrite()` | the access rule as a bool |
| `ByteCount()` | `sizeof` the value type |
| `Ref()` | the `RegisterRef` a bus receives: device, address, address width and table |

## Byte packing

A register's bytes are packed and unpacked by `ByteConverter`, which is also the only byte-order code of VHAL:
`ByteConverter::ToBytes<T, order>(value)` and `ByteConverter::FromBytes<T, order>(bytes)`. Both are `constexpr`, so a constant
can be checked at compile time. See [ByteConverter](../ByteConverter).

```cpp
constexpr std::array<uint8, 2> packed = ByteConverter::ToBytes<uint16, ByteOrder::big>(uint16(0x1234));   // 12 34
static_assert(packed[0] == 0x12);
```
