# SPIAdapterF4

The [SPI adapter](../../../SPIAdapter.md) on the STM32F4 SPI peripheral, master or slave, 8-bit frames.

Header: `#include <Adapter/Port/STM32/F4/SPIAdapterF4.h>`

Class: `SPIAdapterF4`. Alias: `ASPI`. The constructor takes the peripheral handle (`SPI1`, `SPI2`, ...) and the bus clock in hertz. The bus clock sets the prescaler together with `Parameters::maxSpeedHz`.

The BSP enables the instance clock before `SetParameters()`, configures the pins, and forwards `SPIx_IRQn` to `IrqHandler()`.

## What works

| Method | Notes |
|--------|-------|
| `SetParameters()` | Returns `busy` while a transfer runs. Otherwise installs the parameters and returns the prescaler. |
| `Write`, `WriteArray` | Blocking transmit. |
| `WriteAsync`, `WriteArrayAsync` | Interrupt-driven transmit. `TXE` is enabled, and `onInterrupt(Irq::Tx)` fires at the end. A size of 0 returns `invalidParameter`. |
| `WriteRead`, `WriteReadArray` | Blocking full duplex. |
| `WriteReadAsync`, `WriteReadArrayAsync` | Interrupt-driven full duplex. |
| `Read`, `ReadArray` | Blocking receive. The master clocks out `0xFF` for each byte, then waits for the reply. |
| `ReadAsync`, `ReadArrayAsync` | Interrupt-driven receive. The master clocks out dummy bytes (`0xFF`) for each received byte. `onInterrupt(Irq::Rx)` fires at the end. A size of 0 returns `invalidParameter`. |
| `SetContinuousAsyncRxMode(true)` | Receives bytes until stopped. Each received byte raises `onInterrupt(Irq::Rx)`. |
| `SetContinuousAsyncRxMode(false)` | Stops the continuous receive only. A write in progress keeps running. Outside continuous receive it does nothing and returns `ok`. |
| `AbortReceive()`, `AbortTransmit()`, `AbortAll()` | Stop both directions, since they share one clock. Interrupts are masked while the state is cleared. A frame on the wire is allowed to finish and its byte is dropped. |
| `IrqHandler()` | Serves errors first, then receive, then transmit. |

Errors reported through `onError`, when the error interrupt is enabled (which the asynchronous calls do):

| Flag | Error | Cleared by |
|------|-------|------------|
| `OVR` | `Overrun` | Read of `DR`, then `SR`. Reported only while a receive runs. A write does not read the receive register, so the overrun it causes is cleared without a report. |
| `MODF` | `ModeFault` | Read of `SR`, then write of `CR1`. The instance is enabled again. |
| `FRE` | `FrameFormat` | Read of `SR` |

A flag is cleared even when its error interrupt is off, so it cannot hold up the receive.

## Not supported

- Continuous transmit: `SetContinuousAsyncTxMode(true)` returns `notSupported`.
- CRC. The CRC is not enabled. A CRC error flag is cleared but not reported, because the error enum has no CRC entry.
- Transfers longer than 65535 bytes, blocking or asynchronous. The counters are 16 bits, so a longer transfer returns `invalidParameter` and nothing is sent.

## Interrupts and the BSP

The BSP does the following before the first `SetParameters()`:

- Enables the SPI bus clock and the GPIO alternate functions of SCK, MOSI and MISO.
- Enables `SPIx_IRQn` in the NVIC and calls `IrqHandler()` from it.
