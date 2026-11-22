# RtuTransport

The RTU transport over one UART and an optional pair of RS-485 pins. The frame timing, the direction control and the receive ring are
described on the [RTU](README) page. This page covers the class.

```cpp
template<typename Uart, typename Pin, auto (*Make)(), uint16 RxBufferSize = 256>
class RtuTransport;
```

| Parameter | Meaning |
|---|---|
| `Uart` | A UART adapter (`UARTAdapterG4` on STM32 G4) |
| `Pin` | Its GPIO adapter type (`GPIOAdapterG4`). The pins are optional |
| `Make` | A captureless lambda that returns the `RtuBuilder`, with the turnaround, `Lenient()` and the `Clock<&Fn>()` |
| `RxBufferSize` | Receive ring entries. At least 256 |

```cpp
Link link(uart, &dePin);       // DE only
Link link(uart, &dePin, &rePin);   // DE and RE
Link link(uart);               // no pins: no direction control
```

## Methods

| Method | What it does |
|---|---|
| `Begin()` | Once, before the first send. Reads the baud rate, turns on the per-byte receive, replaces the adapter's receive and error callbacks, checks the clock. Returns `error` for a clock that does not advance. When the adapter refuses the receive mode, the callbacks are not replaced |
| `Send(span, timeoutUs)` | Sends one frame with the direction and the turnaround. Waits at most `timeoutUs` for the line to go quiet; returns `timeout` and sends nothing when the line stays busy that long. Returns once the last byte has left the line. A failed write returns `writeError`, also when the adapter reports a timeout after the bytes were handed over: the frame may be on the line, so the caller must not send it again on `timeout` |
| `Receive(span, timeoutUs)` | Copies one complete frame into the span and returns its length. Returns `timeout` when none is ready. `timeoutUs` 0 returns at once |

The client and the server call `Send` and `Receive` themselves. A user calls `Begin`, and nothing else on the transport.

`Begin` turns on the per-byte receive of the adapter (`SetContinuousAsyncRxMode(true)`). On the G4 port a second `Begin` returns
`notAvailable`, so call it once.

## ESP32

On the ESP32 port the per-byte callback runs in a FreeRTOS task, not in the interrupt, so each byte's time is the time the task reads it.
The t1.5 check cannot be trusted there: build the transport with `Lenient()`. Bytes arrive in bursts set by the driver's FIFO threshold
and RX timeout, so the end of a frame is detected late by up to that RX timeout. A frame longer than the FIFO threshold can be split if
the task lags. A hardware RX-timeout path is a follow-up. See the [RTU](README) page.
