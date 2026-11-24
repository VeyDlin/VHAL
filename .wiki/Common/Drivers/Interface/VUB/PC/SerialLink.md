# VUB::PC::SerialLink

A serial port as a byte stream: a UART behind a USB-serial adapter, or a CDC device on the PC.
Header: `Common/Drivers/Interface/VUB/PC/SerialLink.h`. Namespace `VUB::PC`. See the [PC page](README) for the chain and the reopening
rules.

## Traits

`LinkTraits().FullDuplex().ReportsState().MaxPacket(256)`. A stream link: it is not framed and not reliable, so a framing (COBS or a
length prefix) goes above it, and a serial line that drops bytes shows up as a bad frame. The link is up while the port is open.

Members: `Write(span)` returns `Result<std::size_t>`; `Poll(Receiver)`; `SetStateSink(StateSink)`. It satisfies `VUB::StreamLink`,
`VUB::PollingLink` and `VUB::ReportingLink`.

## Device and OS calls

On Windows:

- The port is opened with `CreateFileA` on `\\.\COMn`, with `FILE_FLAG_OVERLAPPED`.
- The line settings are written into a DCB that was read with `GetCommState` (`ToDcb`), then set with `SetCommState`. The flow control is
  off, and `DTR` and `RTS` are enabled.
- `SetCommTimeouts` sets `ReadIntervalTimeout` to `MAXDWORD` with zero totals, so an overlapped `ReadFile` completes at once with the bytes
  that are waiting.
- `EscapeCommFunction(SETDTR)` runs on open, so a CDC device sees the terminal open.
- One overlapped read is started into the link's 64-byte buffer when the previous bytes have been fed. `Poll` feeds up to the receiver's
  room and keeps the rest.
- `Write` copies up to 64 bytes into the link's own buffer and starts an overlapped `WriteFile`. It returns zero while that write is pending.

On Linux:

- The port is opened with `O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC`.
- The termios is set (`ToTermios`): `cfmakeraw`, then the speed, the data bits, the parity and the stop bits, with `CLOCAL` and `CREAD`.
  `DTR` is raised with `TIOCMBIS`, and the input is flushed. The driver's refusal of `TIOCMBIS` is not an error.
- `Poll` reads with `read` up to the receiver's room. `Write` is `write`, and it returns what the kernel took.

## Settings

| Field | Values | Default |
|---|---|---|
| `baud` | any above zero on Windows; on Linux 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600 | 115200 |
| `dataBits` | 5 to 8 | 8 |
| `parity` | `none`, `odd`, `even` | `none` |
| `stopBits` | `one`, `two` | `one` |

`Open` refuses settings that are not valid (`ValidSettings`), with `invalidParameter`, and a baud rate that termios does not have on Linux.

## Port names

`PathOf` gives the path of a port. On Windows, `"11"` and `"COM11"` are `\\.\COM11`, and `\\.\COM10` is kept as it is; a COM number above 9 needs
the `\\.\` form, which `PathOf` adds. On Linux, a name without a slash is taken under `/dev`, so `"ttyUSB0"` is `/dev/ttyUSB0`.

## States

- `Open` reports up. `Close` reports down when the port was open.
- An I/O error in `Poll` or `Write` (a read that returns an error or end of file, a write that fails) closes the port and reports down,
  without waiting. On Windows the transfers in flight are cancelled, and `Poll` frees the handles once they have completed. `Open` again
  brings the link up.
- `Close` waits up to 200 ms for the transfers in flight. The destructor waits until they have completed.
- `Open` on a link that is open closes it first. It returns `busy` while a release from an earlier close is still waiting.

## Snippet

The snippet below is compiled by `TestVubPcWiki.cpp`. It does not run in a test.

```cpp
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/PC/SerialLink.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <span>

// The receive handler: each COBS frame that arrives is one payload.
void OnPortPayload(const VUB::Meta&, std::span<const uint8>) {
}

// The clock, in microseconds. The COBS framing does not read it, so a stack without a timed block could leave it out.
inline uint32 PortClockUs() {
	return 0;
}

using PortStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<VUB::PC::SerialLink>()
		.MaxPayload(64)
		.ReceiveRing(256)
		.OnReceive<&OnPortPayload>()
		.Clock<&PortClockUs>();
}>;

// Opens COM11 at 115200 8N1 and runs the stack once. The application calls Process in its loop.
void RunPort() {
	VUB::PC::SerialLink port;
	PortStack stack(port);
	VUB::PC::SerialLink::Settings settings;
	if (port.Open("COM11", settings) == ResultStatus::ok) {
		stack.Process();
	}
}
```

## Test

`TestVubPcSerial.cpp` checks the settings and the paths on both systems: the DCB fields on Windows, and the termios on Linux. On Linux it
also opens the link on a pseudo-terminal pair (`openpty`): the settings reach the pair, a write comes out of the master, a byte written to
the master is polled into the receiver, and closing the master reports the link down, after which the link opens again. A second part runs
two stacks with COBS framing, each on a link opened on its own pair; the test forwards bytes between the two masters as the wire, and a
packet sent by each stack arrives at the other. The pseudo-terminal driver keeps eight data bits and no parity, so those two settings are
checked only by the pure part.
