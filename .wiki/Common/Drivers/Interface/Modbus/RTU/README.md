# Modbus RTU

Modbus RTU on a shared RS-485 line. The framer adds the slave address and the CRC-16. The transport waits for the frame gaps, drives
the direction pin, and hands the client or the server one whole frame at a time. The client and the server are on the [Modbus](../README)
page, and this page covers the transport and the frame rules.

| Header | What it does | Page |
|---|---|---|
| `RTU/RtuFramer.h` | Frame: slave address, PDU, CRC-16 (low byte first). Check: CRC first, then address | this page |
| `RTU/RtuTiming.h` | Gaps in microseconds for a baud rate (t1.5 and t3.5) | this page |
| `RTU/RtuTransport.h` | The transport: UART, direction pins, receive ring, `Send` and `Receive` | [RtuTransport](RtuTransport) |

The framer has its own CRC-16 table, built at compile time. The shared `Checksum/CRC/Crc.h` builds its table at run time and keeps 512 bytes
of RAM for it, so the framer does not use it.

## Setup

The UART is set up by the user: `uart.SetParameters(...)` gives the baud rate and the frame format. Then `link.Begin()` is called once.
`Begin` reads the baud rate for the gaps, turns on the per-byte receive, and replaces the adapter's receive and error callbacks. A callback
the user installed before `Begin` is replaced; when the adapter refuses the receive mode, the callbacks are left as they were. It also
checks the clock. It returns `error` when the clock does not advance, and `invalidParameter` when the baud rate is 0. Every gap check
depends on the clock, so a dead clock fails at setup.

The clock is a function that returns microseconds since start, as `uint32`. `Clock<&Fn>()` in the builder names it.

```cpp
uint32 MicrosecondClock();                  // the port's free-running microsecond counter

using Link = RtuTransport<UARTAdapterG4, GPIOAdapterG4, +[] {
	return RtuBuilder()
		.Turnaround(2)
		.Clock<&MicrosecondClock>();
}>;

UARTAdapterG4 uart(USART2);
GPIOAdapterG4 dePin(GPIOA, 1);
Link link(uart, &dePin);

uart.SetParameters({ .baudRate = 9600 });
link.Begin();
```

A transport with no pin does no direction control. Use that on a full duplex line, or with a transceiver that switches direction by
itself: `Link link(uart);`.

## Frame gaps

A character is 11 bits on the wire, whatever the port is set to, so parity and stop bits do not change the gaps.

| Gap | Meaning | 9600 baud | 115200 baud |
|---|---|---|---|
| t1.5 | A gap above it inside a frame breaks the frame | 1718 us | 750 us |
| t3.5 | Silence this long ends a frame | 4010 us | 1750 us |

Above 19200 baud the two gaps are fixed at 750 us and 1750 us, as the Modbus over Serial Line specification gives them.

Each received byte has its time, so silence is measured from the last byte. A frame ends when t3.5 has passed with no byte, or when the
next byte starts a new frame. A frame that broke gives `invalidData`.

## Direction and echo

With a direction pin, `Send` does this: it waits for t3.5 of silence since the last byte, clears the receive ring, raises the pin, writes
the bytes, and waits for the last byte to leave the line. It then holds the turnaround (in character times, 0 to 3), drops the pin and
clears the ring again. The second clear drops the echo of the frame when the receiver was on.

With a receiver pin (RE), the receiver is off from the start of the send to the end of the hold, so no echo arrives.

`Turnaround` under 3.5 character times is enforced at compile time. The hold must end before a reply can start.

## Lenient frames

Some devices send a gap above t1.5 inside a frame. `Lenient()` in the builder accepts such a gap. A gap of t3.5 still ends the frame.

```cpp
using Link = RtuTransport<UARTAdapterG4, GPIOAdapterG4, +[] {
	return RtuBuilder()
		.Lenient()
		.Clock<&MicrosecondClock>();
}>;
```

## Receive ring

Each received byte goes into a ring with its frame flags. The ring holds 256 entries by default, which is the largest frame, and the fourth
template argument changes that. A byte that does not fit breaks the frame, and so does a receive error event (frame, noise, parity or
overrun). A ring smaller than 256 is a compile error.

Each entry is 2 bytes (the byte and its frame flags), so the default ring costs 512 bytes of RAM per transport. A frame of 256 bytes
needs 256 entries, and a single byte per entry cannot keep the flags. The transport object with the default ring is 572 B of bss on
Cortex-M4. The `RtuTransport` row of `Common/Drivers/Interface/Modbus/.tool/size/size.sh` measures it (text 1122 B, data 4 B), and the
ring is 512 B of that.

## Interrupts

The receive interrupt stores one byte and its frame flags, and nothing else. There is no idle interrupt. The main loop (the client's calls
and the server's `Process`) takes the frames. The user owns the UART and its vector: the vector calls the port's handler as usual.

The gap checks need each byte's arrival time. On the STM32 G4 port the receive interrupt runs once per byte, so the time is the arrival
time.

On the ESP32 port the per-byte callback runs in a FreeRTOS task, not in the interrupt, so each byte is stamped when the task reads it.
The t1.5 check cannot be trusted there, because the stamp can lag the arrival by more than t1.5 (1718 us at 9600 baud). Use `Lenient()`
on ESP32. A frame that is valid on the wire can still be reported as broken, or end early, because of the lag.

Bytes also arrive in bursts. The driver's FIFO threshold and RX timeout set the bursts, so the end of a frame is detected late by up to
that RX timeout. A frame longer than the FIFO threshold can be split if the task lags. A hardware RX-timeout path is a follow-up.

RTU on ESP32 has not been checked on hardware.

## Errors

| Status | Cause |
|---|---|
| `invalidData` | The frame broke on the line, or a receive error or a full ring broke it |
| `timeout` | No complete frame is ready (from `Receive` with timeout 0, or after the timeout) |
| `writeError` | The transmit failed, including an adapter timeout after the bytes were handed over. The frame may be on the line, so it is not sent again |
| `noInit` | `Send` or `Receive` before `Begin` |
| `invalidParameter` | `Begin` with a baud rate of 0, or `Send` with an empty frame |
