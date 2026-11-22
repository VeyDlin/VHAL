# Modbus

Modbus in layers. A function code is a pure request and reply layout. A framer adds the slave address and the CRC to a PDU and checks
a received frame. A transport puts whole frames on a line with the timing and the direction control. A client sends requests and checks
their replies. A server answers requests from a [`RegisterView`](../../../Utilities/Data/RegisterMap/RegisterView) of user variables.

Stage 1 has the RTU framer only. A TCP or ASCII framer plugs in at the same place, without a change to the client or the server.

| Layer | Header | Page |
|---|---|---|
| Result of a request | `ModbusResult.h` | [ModbusResult](ModbusResult) |
| Function codes, one per code | `Pdu/*.h` | none (no I/O, one header per code) |
| Framer (RTU: address and CRC-16) | `RTU/RtuFramer.h` | [RTU](RTU/README) |
| Transport (line, timing, direction) | `Transport.h` (concept), `RTU/RtuTransport.h` | [RtuTransport](RTU/RtuTransport) |
| Client | `ModbusClient.h` | [ModbusClient](ModbusClient) |
| Server | `ModbusServer.h` | [ModbusServer](ModbusServer) |
| Broadcast (writes to address 0) | `ModbusBroadcast.h` | [ModbusBroadcast](ModbusBroadcast) |
| Register bus (for the register map's templates) | `ModbusRegisterBus.h` | [ModbusRegisterBus](ModbusRegisterBus) |

A master on RTU, slave 1, with an RS-485 direction pin:

```cpp
// The transport: one UART, one direction pin. The clock is the port's microsecond clock.
using Link = RtuTransport<UARTAdapterG4, GPIOAdapterG4, +[] {
	return RtuBuilder()
		.Turnaround(2)
		.Clock<&MicrosecondClock>();
}>;

UARTAdapterG4 uart(USART2);
GPIOAdapterG4 dePin(GPIOA, 1);
Link link(uart, &dePin);

// The client: slave 1, 500 ms per attempt, two retries.
using Plc = ModbusClient<Link, RtuFramer, +[] {
	return ClientBuilder()
		.SlaveAddress(1)
		.Timeout(500)
		.Retries(2);
}>;

Plc plc(link);

std::array<uint16, 4> values{};
ModbusResult reply = plc.ReadHoldingRegisters(0x0000, values);
```

A slave, with a view over user variables and a computed value:

```cpp
extern uint16 setpoint;
bool CheckSetpoint(uint16 value);                       // true when the value is allowed
uint16 ReadChipTemperature();

using Table = RegisterView<+[] {
	return RegisterViewBuilder()
		.HoldingRegisters(0x0000, Variable(&setpoint, &CheckSetpoint))
		.InputRegisters(0x0010, Computed(&ReadChipTemperature));
}>;

using Server = ModbusServer<Link, RtuFramer, Table, +[] {
	return ServerBuilder()
		.SlaveAddress(1);
}>;

Server server(link);
// main loop: server.Process();
```

Each builder takes its settings in a captureless lambda. The lambda is a template argument, so the settings are known at compile time,
and a wrong value is a compile error with a message. The `Make` lambda returns the builder; the class is named by the alias.

## Function codes and tables

A function code is instantiated only when it is used, and the server only for the entry kinds its view has. The table below is what
each kind enables.

| View entry | Function codes |
|---|---|
| `HoldingRegisters` | 03 read, 06 write one, 10 write several |
| `InputRegisters` | 04 read |
| `Coils` | 01 read, 05 write one, 0F write several |
| `DiscreteInputs` | 02 read |

A request for a code the view does not enable gets exception 01.

## Sizes

The CRC-16 table is 512 bytes of flash, built at compile time, and no RAM. A function code costs flash only when the code is used.
The size of a client and a server, with the measured cost of each function code, is on the [ModbusClient](ModbusClient) and
[ModbusServer](ModbusServer) pages.

## Not in stage 1

- Modbus TCP and ASCII. The framer is the only part that changes.
- Function codes 07, 08, 0B, 0C, 11, 14, 15, 16, 17 and 2B.
- A shadow copy of the registers for other tasks.
