# VUB::UartLink

A byte-stream link over a UART port. Header: `Common/Drivers/Interface/VUB/Link/UartLink.h`.

```cpp
template<typename Uart, std::size_t TxBytes = 64, std::size_t MaxPacketBytes = 64>
class UartLink;
```

`Uart` is the port class: `AUART` on the G431 (`UARTAdapterG4`, on USART1), or `UARTAdapterESP` on the ESP32, or any class derived from
the `UARTAdapter<Handle>` base with the same members. `TxBytes` is the size of the link's transmit buffer. `MaxPacketBytes` is the
largest packet of the link, which sets the stack's buffer for a packet of the framing above it (the packet is the payload and the
headers of the layers, not the framing bytes).

## Traits

- Stream: `Framed()` is not set, so a framing goes above the link (COBS, length prefix or gap framing).
- Not reliable: a receive error drops the bytes it hit, and a layer above that needs a guarantee brings its own.
- Full duplex: the receive and the transmit run at once.
- Either side starts: the link does not wait for the other side.
- No state report: the link is always up. A UART has no signal to report.
- `MaxPacket(MaxPacketBytes)`, 64 by default.

## Device API

The link uses these members of the port, all from the `UARTAdapter` base:

| Member | Used by | Context |
|---|---|---|
| `SetOnInterrupt(std::function<void(Irq)>)` | `SetReceiver`, `Write` | main, inside a critical section |
| `SetOnError(std::function<void(Error)>)` | `SetReceiver`, `Write` | main, inside a critical section |
| `SetContinuousAsyncRxMode(bool)` | `SetReceiver` | main |
| `WriteArrayAsync(uint8*, uint32)` | `Write` | main (`Process`) |
| `GetLastRxData()` | the `Irq::Rx` callback | interrupt |

The callbacks carry `Irq::Rx` (a byte has arrived; the byte is `GetLastRxData()`) and `Irq::Tx` (the transfer the link started has
ended). The errors are `Error::Parity`, `Error::Noise`, `Error::Frame` and `Error::Overrun`.

## Receiving

`SetReceiver(receiver)` with an attached receiver starts the continuous receive, `SetContinuousAsyncRxMode(true)`, and installs the
port's two callbacks. From then on each `Irq::Rx` feeds the one byte to the receiver with `Feed`, so the stack counts a byte it has no
room for as `ringFull`. The link owns the port's callbacks while it has a receiver.

`SetReceiver` with an empty receiver (the stack's destructor does this) stops the continuous receive and clears both callbacks.

`ReceiveStatus()` returns the status of the last `SetReceiver`: `ok`, or what the port refused the continuous receive with.

## Transmitting

`Write(bytes)` copies up to `TxBytes` of `bytes` into the link's own buffer, because the port keeps the pointer it is given, and starts one
transfer with `WriteArrayAsync`. It returns the count it took.

- While a transfer runs, `Write` returns 0. The transfer ends at `Irq::Tx`.
- On a port that completes inside `WriteArrayAsync` (the ESP32 port), `Irq::Tx` is raised before the call returns. The link marks the
  transfer as running before the call, so that completion ends it, and the next `Write` is accepted at once.
- A transfer that the port started outside the link makes `Write` return 0 (the port answers busy); the link does not hold the port.
- Another error from the port is returned as the `Write` status.

There is no direction pin (DE) in this link: an RS-485 driver is not enabled by it.

## Errors

`Errors()` returns the receive errors counted since the link was made, one count for each kind:

```cpp
struct UartErrorCounts {
	uint32 parity;
	uint32 noise;
	uint32 frame;
	uint32 overrun;
};
```

The counts are volatile fields written by the error callback. The receive goes on after an error: the bytes around it are fed as they
come.

## Contexts

- `SetReceiver`, `Write`, `Errors` and `ReceiveStatus`: main context (the stack's constructor and `Process`).
- The `Irq::Rx` and `Irq::Tx` callbacks and the `Irq::Rx` byte feed: the port's interrupt on the G4 port, and the `uart_rx` task on the ESP32
  port. A receive byte goes to `Feed`, which is safe from an interrupt.
- `Irq::Tx` clears the running flag, which `Write` reads in main context. The flag is volatile.

## Snippet

The snippet is compiled in `TestVubLinkWiki.cpp`, where the test gives `AUART` the fake port and `USART1` a null handle, since the G4
vendor headers are not in the host tree. On the G431 they are the real names.

```cpp
// On the G431 the port is AUART, the G4 adapter on USART1. The demo runs the link with its console off.
AUART uart(USART1);
VUB::UartLink<AUART> uartLink(uart);

void OnConsolePayload(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
}

// A byte stream with COBS framing. The link is the last block, as every link is.
using Console = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<VUB::UartLink<AUART>>()
		.MaxPayload(32)
		.OnReceive<&OnConsolePayload>();
}>;

Console console(uartLink);
```

## Size

Measured on the host with the port double of the test kit (`sizeof`, g++ 15 on Windows, x86-64): `UartLink<Port, 64, 64>` is 128
bytes, and `UartLink<Port, 8, 64>` is 72 bytes. The port itself is not part of the link. The link holds its receiver, the transmit buffer,
its flags and its four error counts.

## Tests

`TestVubUartLink.cpp` runs the link over `FakeUart`, which derives from the real `UARTAdapter<FakeUartHandle>` base, so the callbacks and
the transfer state are the base's own. It checks the Write rules on both port behaviours, the receive path, the error counts, the
callbacks cleared on detach, and two stacks with COBS and with `VUB::Reliable` over two ports, with a busy transfer that keeps packets in
the stack.
