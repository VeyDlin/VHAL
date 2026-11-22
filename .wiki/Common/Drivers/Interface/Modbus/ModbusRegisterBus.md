# ModbusRegisterBus

Puts a Modbus client under the register map's free templates, so a register described once can be read and written over Modbus,
I2C or SPI. The register's table picks the function code.

```cpp
Plc plc(link);
ModbusRegisterBus<Plc> bus(plc);

constexpr auto Setpoint = RegisterDescriptor<uint16>(0, 0x0000).Table(RegisterTable::Holding);
constexpr auto Output = RegisterDescriptor<uint8>(0, 0x0005).Table(RegisterTable::Coil);
constexpr auto Alarm = RegisterDescriptor<uint8>(0, 0x0006).Table(RegisterTable::Discrete).ReadOnly();

Result<uint16> value = Read<Setpoint>(bus);     // function 03
Write<Setpoint>(bus, 100);                      // function 06 (one register)
Write<Output>(bus, 1);                          // function 05
Result<uint8> state = Read<Alarm>(bus);         // function 02
```

| Table | Read | Write |
|---|---|---|
| Holding | 03 | 06 for one register, 10 for two or more |
| Input | 04 | not supported |
| Coil | 01 | 05 (one coil, bit 0 of the byte) |
| Discrete | 02 | not supported |

The device address of the descriptor is ignored on Modbus, and so is its address width. The slave address is the client's. A
32-bit value is two registers: `RegisterDescriptor<uint32>(0, 0x0020)` reads and writes registers 0x0020 and 0x0021, most significant
first, with the descriptor's byte order.

A coil or discrete register takes one byte, and its bit is bit 0. A word register takes an even number of bytes, one word each.
Other sizes return `invalidParameter` before any traffic.

The bus keeps a pointer to the client, and the client must outlive the bus.
