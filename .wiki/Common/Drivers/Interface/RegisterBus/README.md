# RegisterBus

Register access over I2C and SPI. Each bus wraps an adapter and meets the [`RegisterBus`](../../../Utilities/Data/RegisterMap/RegisterBus)
concept, so the same `Read`, `Write` and `Modify` templates work on every bus. A descriptor does not change between buses.

| Header | Bus | Page |
|---|---|---|
| `I2CRegisterBus.h` | `I2CAdapter`, through its `ReadArray` and `WriteArray` | [I2CRegisterBus](I2CRegisterBus) |
| `SpiRegisterBus.h` | `SPIAdapter`, through `Continuous`, `WriteArray` and `ReadArray` | [SpiRegisterBus](SpiRegisterBus) |

The descriptor and the access functions are described in [RegisterMap](../../../Utilities/Data/RegisterMap/README).

```cpp
#include <Drivers/Interface/RegisterBus/I2CRegisterBus.h>
#include <Utilities/Data/RegisterMap/RegisterAccess.h>

constexpr auto Measured = RegisterDescriptor<uint16>(0x68, 0x3B).ReadOnly();

I2CRegisterBus<I2CAdapterG4> bus(i2c);
Result<uint16> value = Read<Measured>(bus);
```

The bus keeps a reference to its adapter, and the adapter must outlive the bus. Each bus is a template on the adapter type, so a
bus for an adapter nobody uses costs nothing.
