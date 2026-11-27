# ModbusServer

A Modbus server over a transport and a framer. It answers the requests for the entries of a [`RegisterView`](../../../Utilities/Data/RegisterMap/RegisterView).
The server owns no data: the view points at the user's variables and functions.

```cpp
extern uint16 setpoint;
bool CheckSetpoint(uint16 value);
uint16 ReadChipTemperature();
bool relayOn;

using Table = RegisterView<+[] {
	return RegisterViewBuilder()
		.HoldingRegisters(0x0000, Variable(&setpoint, &CheckSetpoint))
		.InputRegisters(0x0010, Computed(&ReadChipTemperature))
		.Coils(0x0000, Variable(&relayOn));
}>;

using Server = ModbusServer<Link, RtuFramer, Table, +[] {
	return ServerBuilder()
		.SlaveAddress(1)
		.MaxCoilWrite(64);
}>;

Server server(link);

// main loop:
ResultStatus status = server.Process();
```

## Settings

| Setting | Default | Rule (compile error when broken) |
|---|---|---|
| `SlaveAddress(n)` | 1 | 1 to 247 |
| `MaxCoilWrite(n)` | 1968 | 1 to 1968. A coil write over this count gets exception 03. The server keeps `n` bools in RAM when the view has coils |

## Process

`Process()` takes at most one frame from the transport, answers it, and returns:

| Status | Meaning |
|---|---|
| `ok` | A frame was handled, with a reply or without one (a broadcast, or a frame for another slave) |
| `timeout` | No complete frame is ready yet |
| `crcError` | A frame with a bad CRC. No reply |
| `invalidData` | A frame that broke on the line. No reply |
| `writeError` | The reply could not be sent |

The main loop calls `Process()` often. It never waits for a frame.

## Function codes

A function code is served only when the view has the entry kind it needs: holding registers enable 03, 06 and 10; input registers
enable 04; coils enable 01, 05 and 0F; discrete inputs enable 02. Any other code, and a code whose table the view does not have, gets
exception 01.

## Exceptions

| Exception | When |
|---|---|
| 01 | Function code not enabled by the view |
| 02 | A register or coil outside the view, a number or single bool entry that a write does not cover whole, a read-only entry in a write, or a range that wraps past 0xFFFF |
| 03 | Quantity of zero or over the limit, a bad byte count, a value 05 does not accept (only 0xFF00 and 0x0000), or a value a check rejects |
| 04 | A set function failed in the middle of a write |

## Writes are all or nothing

Before a write changes anything, the view checks the whole request: the range is inside the view, each entry is writable, and each entry's
check accepts its value. A failure in those checks changes nothing. A set function can still fail after an earlier entry was written, and
then the reply is exception 04. A device has no rollback, so the entries before the failure stay written.

A write must cover each number or single bool entry it touches whole. A multi-register write that stops in the middle of a 32-bit entry
gets exception 02. An array entry may be written in part: FC05 to one coil of a coil block, or FC06 to one element of a register block,
changes that element only. A partial write of an array reads the entry twice, once per pass, so a computed value runs twice. An
uncovered element that the device changes between the two reads keeps its new value (see RegisterView).

## Broadcast and other slaves

A broadcast (address 0) runs the write codes 05, 06, 0F and 10 and sends no reply. A read to address 0 is ignored. A frame for another
slave, and a frame with a bad CRC, get no reply.

## Sizes

The numbers are the output of `Common/Drivers/Interface/Modbus/.test/size/size.cmake`, one probe per row (the `.cpp` files in that folder).
The script builds each probe with `arm-none-eabi-g++` 15.2 for a Cortex-M4, `-std=gnu++23 -mcpu=cortex-m4 -mthumb -fno-rtti
-fno-exceptions -Os`, and prints the sections `arm-none-eabi-size` reads. The probe transport is declared and not defined, and each
probe is an object file, so no UART and no link are in the numbers. The text column includes the 512-byte CRC table once. The data column
is the 4-byte init array of the probe's global server object.

| Probe | text | data | bss |
|---|---|---|---|
| Server with no entries (the fixed part: framing, `Process`, exception replies) | 792 B | 4 B | 520 B |
| Holding registers only (03, 06, 10) | 1456 B | 4 B | 768 B |
| Input registers only (04) | 1026 B | 4 B | 768 B |
| Discrete inputs only (02) | 1028 B | 4 B | 520 B |
| Coils only (01, 05, 0F), default `MaxCoilWrite` | 1540 B | 4 B | 2488 B |
| Coils only, `MaxCoilWrite(64)` | 1540 B | 4 B | 584 B |
| Every entry kind (01 to 06, 0F, 10) | 2914 B | 4 B | 2736 B |

The added cost of each table over the fixed part: holding registers 664 B of text and 248 B of bss, and input registers 234 B of text
and 248 B of bss. Both add the 125-word buffer (250 bytes) that reads and FC 10 use; the bss figures are measured deltas of whole objects. Discrete inputs add 236 B
of text and no bss. Coils add 748 B of text and 1968 B of bss, the bool buffer of FC 0F at one byte per coil. `MaxCoilWrite` sets that
buffer: 64 coils cost 64 B of bss.

The costs do not add up. The framing is shared, but one server with every kind costs 2914 B of text, against 2674 B from the fixed part
and the four single-table costs added together.

The table enables its function codes together, so the cost of one code (03 alone, say) is not measured on its own.
