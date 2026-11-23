# VUB::Gap

Gap framing, as Modbus RTU frames: a packet is the bytes that come with no silence of the gap or longer between them. A silence of the
gap or more ends the packet. Nothing is added to the packet on the wire.

Header: `Common/Drivers/Interface/VUB/Framing/Gap.h`. The logic is that of `Modbus/RTU/RtuTransport.h`, with the packets of the stack.

## Setting the gap

| Option | Meaning |
|---|---|
| `GapUs(us)` | the gap in microseconds; 1750 by default (the Modbus t3.5 above 19200 baud) |
| `GapCharacters(tenths, baud)` | the gap in character times at `baud`: `GapCharacters(35, 9600)` is 3.5 character times |
| `Baud(baud)` | the line's baud rate, for the send side (see below) |
| `WaitTimeoutUs(us)` | the longest a send waits for the gap; 100000 by default |

A character is 11 bits, as in Modbus RTU: 3.5 character times at 9600 baud is 38.5 bits, which is 4010.42 microseconds, and the gap is
rounded up to 4011 microseconds. The rounding is up, so the gap is never shorter than asked. Modbus `RtuTiming` truncates instead and
gives 4010 at this rate. Options with a value of zero are compile errors.

The baud rate can change while the stack runs, for example when the UART's parameters change. The framing then needs the new rate:

```cpp
device.Find<VUB::Gap>().SetBaud(19200);
```

`SetBaud` returns `invalidParameter` for zero and changes nothing. A gap given in character times is recomputed from the new rate.

## Receiving

- The receive interrupt calls `Feed(byte)`, as for every byte-stream chain. In a chain whose framing uses time, `Feed` also reads the
  clock, so the clock must be safe to read in the interrupt.
- `Process` takes the bytes in order, with the time each arrived. It then polls the framing with the time of the call, so a packet ends
  in silence even when nothing comes after it.
- A byte that comes after the gap ends the packet before it. That byte starts the next packet.

## Sending

A new packet waits for the gap:

- since the last byte received, and
- since the line was last busy with the bytes sent.

The link's `Write` says how many bytes it took, not when they leave the line. So the framing assumes that the bytes it sent leave at
the baud rate, one after another from the time they were taken, at 11 bits each. Without a baud rate, it assumes they leave at once.

The end of the bytes is rounded up to a whole microsecond, so the estimate is never short. For example, 100 bytes at 230400 baud take
4774.3 microseconds, and the line is free 4775 microseconds after they were taken.

A wait is bounded by `WaitTimeoutUs`. After that the packet is sent anyway, so a line that stays busy never holds the stack.

## Losses

A lost byte makes a packet come out short, and the gap cannot tell that. The integrity of the payload is the job of a transform above the
framing, such as a CRC.

## Growth

`Growth` is 0: a packet is its payload.

## Link state

`Reset()` drops the partial packet, and the byte that came after the gap and started the next one. The stack calls it when the link goes up
or down.

## RAM

The framing object is 36 bytes on the Cortex-M4 (`sizeof` of `VUB::Gap`, measured with `arm-none-eabi-g++` 15.2.1, `-std=c++20`). It holds the
gap, the baud rate that `SetBaud` changes, the times of the last byte received and of the end of the last send, the wait and the
carried byte, and the packet being received. The options that do not change, the wait timeout and the character count of a gap given in
character times, are template arguments of the framing, so they take no RAM. A chain whose framing uses time keeps the time of each
received byte in the receive ring, 4 bytes per slot: 1024 bytes for the default ring of 256 slots. A chain without a time-using framing
keeps no time and pays nothing for it.

## Example

```cpp
struct Uart {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().MaxPacket(64).FullDuplex();


	Result<std::size_t> Write(std::span<const uint8> bytes) {
		return bytes.size();
	}
};


void OnPacket(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
}


// The user's microsecond clock. A timer counter in a real device; a constant here.
uint32 MicrosecondClock() {
	return 0;
}


using Device = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Gap>()
		.GapCharacters(35, 9600)
		.Link<Uart>()
		.MaxPayload(64)
		.OnReceive<&OnPacket>()
		.Clock<&MicrosecondClock>();
}>;


Uart uart;
Device device(uart);


// The receive interrupt of the UART calls this for each byte.
void OnUartByte(uint8 byte) {
	device.Feed(byte);
}


// After the UART's parameters change, the framing gets the new baud rate.
void OnBaudChanged(uint32 baud) {
	device.Find<VUB::Gap>().SetBaud(baud);
}


void Run() {
	const std::array<uint8, 2> hello = {0x11, 0x22};
	device.Send(std::span<const uint8>(hello.data(), hello.size()));
	device.Process();
}
```

The example is compiled and run in `TestVubFramingWiki.cpp`. The timing rules are checked in `TestVubGap.cpp`, and the stacks over a
faulty line and across a link change in `TestVubFramingStacks.cpp`.
