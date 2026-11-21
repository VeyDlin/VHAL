# I2CAdapterF4

The [I2C adapter](../../../I2CAdapter.md) on the STM32F4 I2C peripheral. It is an I2C master: blocking and interrupt-driven register reads and writes, and a bus scan. Slave mode is not implemented.

Header: `#include <Adapter/Port/STM32/F4/I2CAdapterF4.h>`

Class: `I2CAdapterF4`. Alias: `AI2C`. The constructor takes the peripheral handle (`I2C1`, `I2C2`, ...) and the bus clock in hertz. The bus clock is not used: the speed is computed by the LL driver from the APB1 clock.

## What works

| Method | Notes |
|--------|-------|
| `SetParameters()` | Speed `Low`, `Standard`, `Fast`. `DutyCycle::D16_9` is applied. Returns `notSupported` for `FastPlus`, before the user's `beforePeripheryInit` runs. Returns `busy` while an asynchronous transfer runs, and keeps the settings it started with. |
| `CheckDevice(addr, repeat)` | Blocking. Retries `repeat` times. Address `0x00` to `0x7F`. |
| `Scan(list, size)` | Blocking. Probes `0x08` to `0x77` only (the others are reserved by UM10204). Stops when `list` is full. |
| `Write`, `WriteArray`, `WriteByteArray` | Blocking. Register address (1 or 2 bytes, high byte first), then data. |
| `Read`, `ReadArray`, `ReadByteArray` | Blocking. Register address (1 or 2 bytes), then a repeated START once the register bytes have left the shift register, then data. Counts 1, 2 and 3 are handled as the reference manual gives. |
| `WriteByteArrayAsync`, `WriteAsync`, `WriteArrayAsync` | Interrupt-driven. Register address, then data, then STOP. |
| `ReadByteArrayAsync`, `ReadAsync`, `ReadArrayAsync` | Interrupt-driven. Register address, repeated START, then data. A size of 0 returns `invalidParameter`. |
| `Await()` | Waits until the transfer completes or fails. Returns `ok` on completion and `error` on failure. It reads the state on every pass, so it sees the end of a transfer the interrupt handlers finish on another thread. |
| `IrqEventHandler()`, `IrqErrorHandler()`, `IrqHandler()` | `IrqHandler()` routes an enabled error flag to `IrqErrorHandler()` and everything else to `IrqEventHandler()`. |

An asynchronous transfer ends with the state `ready` (completed) or `error` (failed). The next transfer may start from either state. `Await()` returns right away when the state is `ready` or `error`.

The blocking calls take a register address of 1 or 2 bytes. Other sizes return `invalidParameter`, and nothing is sent.

## Not supported

- Slave mode: `StartSlaveListen()`, `StopSlaveListen()`, and the slave branch of the event handler.
- `CheckDeviceAsync()` and `ScanAsync()` return `notSupported`.
- 10-bit addressing.
- `FastPlus` (1 MHz).
- Asynchronous transfers with a register address longer than 2 bytes (`invalidParameter`), and blocking transfers with a register address of 0 or of more than 2 bytes (`invalidParameter`). An asynchronous read may use a register address of 0 and then starts at once.
- Continuous reception.
- DMA. The `Write` and `Read` paths are all interrupt or polling.

## Interrupts and the BSP

The BSP does the following before the first `SetParameters()`:

- Enables the I2C bus clock and the GPIO alternate function of SCL and SDA, in open-drain mode.
- Enables `I2Cx_EV_IRQn` and `I2Cx_ER_IRQn` in the NVIC, and calls `IrqHandler()` from both vectors.

`IrqHandler()` serves the error vector and the event vector alike. The event handler is called when no error flag is raised with its interrupt enabled.

## Errors

`onError` receives one `Error` per flag found in an error interrupt:

| Flag | Error | Effect |
|------|-------|--------|
| `BERR` | `MisplacedStartStop` | Flag cleared, STOP sent, transfer ends with `error` |
| `ARLO` | `ArbitrationLost` | Flag cleared, transfer ends with `error`. No STOP: the bus is released by the hardware. |
| `AF` | `AcknowledgeFailure` | Flag cleared, STOP sent, transfer ends with `error` |
| `OVR` | `OverUnderRun` | Flag cleared, STOP sent, transfer ends with `error` |

A STOP is sent only while a transfer runs, so the bus is not left held after an overrun during a read.

The blocking calls return `error` or `timeout` as before, and use the timeout set with `SetTimeout()`.

## Callbacks

| Callback | When |
|----------|------|
| `onComplete` | An asynchronous transfer completed. Called after the state is set to ready. |
| `onError` | An error flag was served (see above). |
