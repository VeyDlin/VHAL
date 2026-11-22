# RegisterSet

Header: `#include <Utilities/Data/RegisterMap/RegisterSet.h>`

A list of register descriptors with the duplicate check. Two registers with one table, one device address and one register
address are an error, because a bus would reach them both through one address.

```cpp
constexpr auto ChargeControl = RegisterDescriptor<uint8>(0x09, 0x01).ReadWrite();
constexpr auto ChargeStatus = RegisterDescriptor<uint8>(0x09, 0x02).ReadOnly();

constexpr RegisterSet<ChargeControl, ChargeStatus> board{};
static_assert(board.count == 2);
```

The check runs when the set is instantiated. A bare list of descriptors has no check, so declare the set as a `constexpr`
object, or use it in a `static_assert`. A duplicate gives the message:

```text
two registers share a table, device and address
```

The check compares the table, the device address and the register address. Every descriptor takes its device address in the
constructor, and the Modbus tables ignore it. Give the Modbus descriptors of one set the same device value: the check compares the
device, so two holding registers at one address with different device values would pass it. A holding register and a coil at one
Modbus address are two registers, because their tables differ, and they may share a set.
