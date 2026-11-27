# EEPROM

A serial EEPROM on an I2C bus, with the records a firmware keeps in it. The driver has three parts: the chip classes, which give the geometry and the device address of a chip; `Eeprom`, which moves bytes in and out of the device; and `Data<T>`, which places one record in the device and reads or writes it.

Headers:

- `#include <Drivers/Memory/EEPROM/Eeprom.h>`: the base class and the `Geometry` of a chip
- `#include <Drivers/Memory/EEPROM/Data.h>`: a record, `Data<T>`
- `#include <Drivers/Memory/EEPROM/IC/AT24C256C.h>` and `#include <Drivers/Memory/EEPROM/IC/24AA02E48.h>`: the chip classes

```cpp
EEPROM::AT24C256C eeprom(BSP::i2c1, false, false, false);   // 7-bit address 0x50

if (eeprom.IsMemoryReady() != ResultStatus::ok) {
    return;
}

EEPROM::Data<Settings> settings(eeprom);
if (settings.GetAllocation() != ResultStatus::ok) {
    return;
}

settings.Set(value);
Result<Settings> loaded = settings.Get();
```

## Chip Classes

| Class | Capacity | Page | Word address | 7-bit address | Write cycle (max) |
|-------|----------|------|--------------|---------------|-------------------|
| `AT24C256C(i2c, a0, a1, a2)` | 32768 bytes | 64 bytes | 2 bytes | `0x50` plus the pins in bits 2:0 | 5 ms |
| `Chip24AA02E48(i2c)` | 256 bytes | 8 bytes | 1 byte | `0x50` | 5 ms |

The chip classes pass a 7-bit device address to the I2C adapter. The adapter port builds the control byte (`0xA0` for a write at `0x50`). A class never stores the 8-bit control byte, because the adapter would shift it a second time.

Datasheet values used here:

- AT24C256C: 256 Kbit, 64-byte page write, a two-byte word address, and the device address `1010 A2 A1 A0 R/W`. A write cycle takes up to 5 ms. A write that crosses the page end rolls over inside the page. Sources: [Microchip device addressing](https://onlinedocs.microchip.com/oxy/GUID-FEEC2B39-7B05-43C4-AB92-C76A6F28FB70-en-US-2/GUID-10D69718-8BB5-49ED-90C1-D1FCE55222A8.html) and [Microchip page write](https://onlinedocs.microchip.com/oxy/GUID-FEEC2B39-7B05-43C4-AB92-C76A6F28FB70-en-US-2/GUID-EE1346FF-A5FE-4EE8-A6E8-B5A41D33EFF3.html).
- 24AA02E48: 2 Kbit, an 8-byte page write, a one-byte word address, and the A0 to A2 pins are not connected, so the device address is fixed. The write cycle is 5 ms maximum. Source: the Microchip 24AA02E48 datasheet. These figures were taken from search excerpts and are not yet checked against the full document; check them against the Microchip datasheet before relying on them for a new design.

The EUI-48 node address of the 24AA02E48 is not modelled.

To add a chip, derive from `Eeprom` and pass the 7-bit address and a `Geometry` (capacity, page size, address bytes, write cycle in ms) from the datasheet. A geometry the driver cannot address is refused (see Eeprom below).

## Writes

`WriteMemory()` first waits for the write cycle of an earlier write, so a chip that is still busy is not written to. It then splits the write at page edges. Each page write is followed by an ACK poll: the driver calls `CheckDevice()` until the chip acknowledges, or until the write cycle time of the datasheet has passed. The last poll is made at the deadline. A chip that does not answer in time gives `timeout`, whether it is busy or absent. The limit is checked after each poll, so a port call that blocks for its own timeout can go past it.

The status of a write is the port's status. The host model returns `nack` at once for a NACK on the write. The STM32 F4 port never returns `nack`: the status of its address phase is overwritten before it is checked (`WriteByteArray` in `I2CAdapterF4.h`), so an address NACK can be lost. This is a defect of that port and is not fixed by this driver.

`Data<T>::Set()` repeats a failed write up to `retry` times and returns the last status when they are used up. Each repeat waits for the write cycle first. A `timeout` is not repeated, because the chip did not finish its write cycle in time.

## Eeprom

| Method | Returns | Description |
|--------|---------|-------------|
| `IsMemoryReady()` | `ResultStatus` | `ok` when the chip acknowledges its address |
| `ReadMemory(data, size, address)` | `ResultStatus` | Reads `size` bytes. `outOfRange` when the range leaves the device, `invalidParameter` for a null buffer |
| `WriteMemory(data, size, address)` | `ResultStatus` | Writes `size` bytes in page-sized pieces, each followed by the write cycle wait. Same range rules |
| `GetGeometry()` | `Geometry` | The capacity, page size, address bytes and write cycle of the chip |
| `SetAddress(address7)` | `void` | Changes the 7-bit device address |
| `retry` | `uint8` | Attempts for each `Data<T>` call. The default is 1 |

Raw calls take the byte address of the device. A zero-size call returns `ok` and does nothing.

Every call checks the chip before the bus is called. The checks, in order:

- `noInit`: no chip is attached.
- `badAddress`: the 7-bit address is `0x80` or more, so an 8-bit control byte such as `0xA0` is never sent.
- `invalidParameter`: a capacity of 0, a capacity above 65536 bytes, a page size of 0, a word-address size other than 1 or 2 bytes, or a one-byte word address with more than 256 bytes.

`Eeprom` cannot be copied: two copies would hand out the same bytes to two records.

## Data

`Data<T>` is one record, placed at the next free address of its device when it is constructed. The constructor is explicit, so an `Eeprom` does not convert to a record by accident.

| Method | Returns | Description |
|--------|---------|-------------|
| `Data(eeprom)` | | Reserves `sizeof(T)` bytes after the last reserved byte |
| `GetAllocation()` | `ResultStatus` | `ok` when the record is placed, `outOfRange` when it does not fit, `noInit` for the default constructor |
| `GetAddress()` | `Result<uint32>` | The first byte of the record. An error, the allocation status, when the record is not placed |
| `Get()` | `Result<T>` | Reads the record. An error is returned, never a zero value |
| `Set(value)` | `ResultStatus` | Writes the record |
| `==`, `!=`, `<`, `>` | `Result<bool>` | Compares the stored record with the argument. An error is returned when the read fails |

Rules:

- A record that fills the device exactly is placed. One byte more is `outOfRange`.
- Every call on a record that is not placed returns its allocation status. Nothing reaches the bus.
- There is no conversion to `T` or to `Result<T>`, no assignment from `T`, and no `++` or `--`. Each of these would hide a bus error. Write the update as two calls: `Get()`, change the value, then `Set()`. A power cut between the two calls leaves the old value or the new one, and the update is not atomic.
- `T` must be trivially copyable (checked by `static_assert`). The bytes of `T` go to the bus as they are, so padding bytes are written too. Use packed types for a record with padding.

## Layout Limit

The address of each record is set by the order of construction: the first `Data<T>` takes address 0, the next takes the address after it, and so on. Reorder the declarations, change the size of one record, or add a record in the middle, and every later record reads the bytes that belonged to another record. The records have no names and no version numbers, so the driver cannot detect the change.

Until a layout is built at compile time with a header for each record, a firmware that changes its records must erase or migrate the device itself.

## Address Rule

The I2C adapters take 7-bit device addresses. The driver refuses an address of `0x80` or more with `badAddress` before the bus is called. The ESP32 port refuses an 8-bit address too. The STM32 F4 port does not check it, and it shifts the value left once more, so `0xA0` goes out as `0x40`. Pass `0x50` (or `0x50` plus the pins), never the control byte.

## Breaking Changes

These change the API of the driver as it stood before this rework.

- `Eeprom()` and `Eeprom(AI2C&)` are removed. `Eeprom(AI2C&, uint8 address7, Geometry)` replaces them.
- `GetMaxAddressPointer()`, `GetMemoryAddressSize()` and the `AddressSize` enum are removed. `Geometry` replaces them.
- `ReadMemory()` and `WriteMemory()` take `uint32` sizes and addresses (they were `uint16`). `WriteMemory()` takes a `const uint8 *`.
- `GetMemoryAddress()` is private and returns `Result<uint32>`.
- The chip classes have no default constructor, and they pass a 7-bit address with the `0x50` base. They used to pass the 8-bit control byte.
- `AT24C256C`: the pin arguments set bits 0 to 2 of the 7-bit address. They used to set bits 1 to 3 of the control byte, which the adapter then shifted again.
- `_24AA02E48` is renamed `Chip24AA02E48`, and its A0 to A2 arguments are removed, because those pins are not connected.
- `Eeprom` is not copyable. `Data(Eeprom&)` is explicit.
- `Data<T>`: the members are private. `dataSize` is a `static constexpr uint32` (it was a `uint16` member). `GetAddress()` returns `Result<uint32>` and is an error when the record is not placed. `GetAllocation()` is new.
- `Data<T>`: `GetData()`, the conversions to `Result<T>` and to `T`, `operator=` and `operator==` that returns `bool` are removed. The comparisons return `Result<bool>`. `++` and `--` are removed.
- `Data<T>::Set()` does not repeat a `timeout`.

## Tests

`.test` holds host tests (`TestEeprom.cpp`). The shim has a fake I2C bus with a 24xx chip behind it. The chip models the page wrap, the write cycle with NACK while busy, the 7-bit device address, and the capacity. The tests cover the page split, the ACK poll (its probe count and its timeout), a busy chip at call time, the retry rules (a timeout is not repeated), the geometry and address checks, the raw bounds, the exact fill of a device, the comparisons, and the compile-time rules of `Data<T>` and `Eeprom`.

Run them with `./test.cmd EEPROM` from the repository root.
