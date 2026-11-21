# ADCAdapterF4

The [ADC adapter](../../../ADCAdapter.md) on the STM32F4 ADC. The regular group is supported; the injected group, the watchdog and calibration are not.

Header: `#include <Adapter/Port/STM32/F4/ADCAdapterF4.h>`

Class: `ADCAdapterF4`. Alias: `AADC`. The constructor takes the ADC handle (`ADC1`, `ADC2` or `ADC3`).

The BSP does the following before the first `SetParameters()`:

- Enables the ADC clock and sets the input pins to analog mode.
- Enables the ADC interrupt in the NVIC and calls `IrqHandler()` from it, when the asynchronous calls are used.

## What works

| Method | Notes |
|--------|-------|
| `SetParameters()` | Resolution 6, 8, 10 or 12 bits; data alignment; scan mode. |
| `ConfigRegularGroup()`, `SetRegularChannel()` | Regular sequence of up to 16 channels, single or continuous conversion, software or external trigger. |
| `Read<T>()`, `ReadArray<T>()` | Blocking single read, see below. |
| `ReadAsync<T>()`, `ReadArrayAsync<T>()` | Interrupt-driven read of the regular group. |
| `AbortRegular()` | Stops the asynchronous regular read. |
| `IrqHandler()` | Serves the end of a regular conversion and the overrun flag. The watchdog and injected handlers run only when their interrupts are enabled, which this adapter does not do. |

### Blocking read

The blocking read starts one software conversion when the regular trigger is software, then waits for each end of conversion with the adapter timeout (`SetTimeout()`). Each result takes the byte count of the resolution, low byte first. A read of `size` bytes gets `size / bytes` results.

The sequence of the regular group converts once per start, so a read asks for at most as many results as the group has ranks. A read that asks for more ends with `timeout`. With an external trigger the adapter does not start the conversion; the read waits for the trigger and times out if none comes.

The first use enables the ADC and waits for the stabilisation delay. The blocking read returns `busy` while an asynchronous read runs, and `invalidParameter` when the size is less than one result.

### Asynchronous read

The asynchronous read has the same size rule: a size below one result returns `invalidParameter`, and the conversion is not started. A buffer of one byte at 12-bit resolution is refused, since one result takes two bytes.

## Not supported

- The injected group: `ConfigInjectedGroup()` and `SetInjectedChannel()` return `notSupported`.
- Calibration: `Calibration()` returns `notSupported`.
- Watchdog: `AbortWatchDog()` clears the flag, but the watchdog is not configured by this adapter.
- Sampling and configuration-ready interrupts: `AbortSampling()` and `AbortConfigurationReady()` do nothing.
- DMA. The asynchronous read is interrupt-driven, one result per interrupt.
