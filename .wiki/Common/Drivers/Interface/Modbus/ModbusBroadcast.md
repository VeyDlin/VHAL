# ModbusBroadcast

The write functions of a client, sent to slave address 0. Every slave on the line receives the write, and none replies. There are
no broadcast reads: a read has no meaning when every slave answers at once.

```cpp
Plc plc(link);

ModbusResult sent = plc.Broadcast().WriteSingleRegister(0x0000, 1234);
plc.Broadcast().WriteMultipleCoils(0x0010, coils);
```

| Function | Code |
|---|---|
| `WriteSingleCoil(address, bool)` | 05 |
| `WriteSingleRegister(address, value)` | 06 |
| `WriteMultipleCoils(address, span<const bool>)` | 0F |
| `WriteMultipleRegisters(address, span<const uint16>)` | 10 |

A broadcast returns `ok` once the frame has left the line and the turnaround has passed. No reply is read, so there is no timeout and
no retry. The next request waits t3.5 of silence as usual, so a broadcast never joins a reply.

A broadcast read does not compile:

```cpp
plc.Broadcast().ReadHoldingRegisters(0x0000, values);   // error: has no member named 'ReadHoldingRegisters'
```

The server on each slave does the write and sends nothing back. A broadcast that a slave rejects is silent.
