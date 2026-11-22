# SpiRegisterBus

Header: `#include <Drivers/Interface/RegisterBus/SpiRegisterBus.h>`

The register bus over an SPI adapter. One bus object serves one chip, because one adapter has one chip-select pin. The
framing is a builder, passed as a captureless lambda:

```cpp
using Bus = SpiRegisterBus<SPIAdapterG4, +[] {
	return SpiFramingBuilder().ReadBit(7).AddressBits(7);
}>;

Bus bus(spi);
constexpr auto Measured = RegisterDescriptor<uint16>(0x68, 0x3B).ReadOnly();
Result<uint16> value = Read<Measured>(bus);
```

Each access is one chip-select window. The command byte goes out first, then the data, with `Continuous(true)` holding
the window open. A read sends the command with the read flag set, then receives the data. A write sends the command with
the flag clear, then the data.

## Framing

| Builder method | Default | Meaning |
|---|---|---|
| `ReadBit(position)` | 7 | the bit of the command byte that marks a read. 0 to 7 |
| `AddressBits(count)` | 7 | the number of low bits that carry the register address. 1 to 7 |
| `BitOrder(order)` | `MsbFirst` | the order the command byte's bits go out in. `LsbFirst` sends it bit-reversed |

The read bit must be at or above the address bits. `ReadBit(3)` with 7 address bits is the error
`the read bit must not be one of the address bits`.

The command byte is the address in the low bits, with the read flag at `ReadBit`. With the defaults, reading 0x3B sends `0xBB`
(`0x80 | 0x3B`). With `LsbFirst` the same byte goes out as `0xDD`.

## Limits

- The descriptor's address must fit the address bits. A larger address returns `invalidParameter`, and nothing is sent.
- A two-byte address width returns `notSupported`, and nothing is sent.
- The device address is ignored. The chip is selected by the adapter's chip-select pin.
