# I2CRegisterBus

Header: `#include <Drivers/Interface/RegisterBus/I2CRegisterBus.h>`

The register bus over an I2C adapter. `Adapter` is an `I2CAdapter<...>` of the port, or any class with the same `ReadArray`
and `WriteArray`.

```cpp
constexpr auto Measured = RegisterDescriptor<uint16>(0x68, 0x3B).ReadOnly();
constexpr auto Wide = RegisterDescriptor<uint8>(0x50, 0x0102).AddressWidth(2);

I2CRegisterBus<I2CAdapterG4> bus(i2c);

Result<uint16> value = Read<Measured>(bus);   // address byte 0x3B, two bytes back, big-endian
Write<Wide>(bus, 0x7E);                       // address bytes 01 02, one byte out
```

The device address comes from the descriptor, 7 bits. The register address size comes from the address width: one byte, or two
bytes, most significant first. A one-byte width with an address above 0xFF returns `invalidParameter`, and nothing is sent.
Any other width returns `invalidParameter`.

A bus error from the adapter is returned as the access's status.
