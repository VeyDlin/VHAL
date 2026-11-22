# RegisterBus

Header: `#include <Utilities/Data/RegisterMap/RegisterBus.h>`

The concept a bus meets for the free templates in [`RegisterAccess.h`](RegisterAccess). It moves the bytes of one register.

```cpp
template<typename Bus>
concept RegisterBus = requires(Bus& bus, RegisterRef where, std::span<uint8> in, std::span<const uint8> out) {
	{ bus.ReadRegs(where, in) } -> std::same_as<ResultStatus>;
	{ bus.WriteRegs(where, out) } -> std::same_as<ResultStatus>;
};
```

`RegisterRef` is what a bus receives for one register:

| Field | Used by |
|---|---|
| `device` (7-bit I2C address) | I2C |
| `address` | all |
| `addressWidth` (1 or 2 bytes) | I2C |
| `table` (`Holding`, `Input`, `Coil`, `Discrete`) | Modbus |

The bytes move in the order the device sends them. The free templates apply the descriptor's byte order.

The buses for this project are in `Common/Drivers/Interface/RegisterBus/`: `I2CRegisterBus.h` and `SpiRegisterBus.h`.
