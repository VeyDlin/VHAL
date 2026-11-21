# DACAdapterF4

The [DAC adapter](../../../DACAdapter.md) on the STM32F4 DAC, one channel per adapter object.

Header: `#include <Adapter/Port/STM32/F4/DACAdapterF4.h>`

Class: `DACAdapterF4`. Alias: `ADAC`. The constructor takes the DAC handle and the channel (1 or 2).

The BSP does the following before the first `SetParameters()`:

- Enables the DAC clock and sets the output pin to analog mode.
- Enables `DAC_IRQn` (or `TIM6_DAC_IRQn` on the chips that share it) in the NVIC and calls `IrqHandler()` from it.

## Trigger source

An unset `Parameters::triggerSource` starts the channel on software. A named `TriggerSource` is written as its register code. `TriggerSource::Timer6Trigger` is the code zero, so it selects TIM6 and is not the same as leaving the option unset:

```cpp
ADAC::Parameters parameters;
parameters.triggerSource = ADAC::TriggerSource::Timer2Trigger;
```

The trigger takes effect only after `EnableTrigger()`.

## What works

| Method | Notes |
|--------|-------|
| `SetParameters()` | Output buffer, trigger source. The channel is enabled only when the initialization succeeds. The base signature has no result, so a refused configuration leaves the channel off without a report. |
| `Write(value)` | Writes a 12-bit right-aligned value. |
| `Enable()`, `Disable()` | `Enable()` waits for the output to settle. |
| `EnableDMARequest()`, `DisableDMARequest()` | Sets or clears the channel's DMA request bit. The DMA stream is not configured by this adapter. |
| `EnableTrigger()`, `DisableTrigger()` | Sets or clears the trigger enable bit. |
| `GetDataRegisterAddress()` | The 12-bit right-aligned holding register, for a DMA peripheral address. |
| `IrqHandler()` | Reports a DMA underrun (`Error::DmaUnderrun` through `onError`) and clears its flag. |

The underrun flag is reported whether or not its interrupt is enabled, since this adapter never enables it.

## Not supported

- `WriteContinuous()` and `StopDMA()` return `notSupported`. The F4 channels have a DMA request, but the stream and channel table of the DMA contract is not in the repository yet, so continuous output is not set up.
- Triggers from TIM3 and TIM1 are not in the F4 DAC's trigger table.
- The wave generator (`WaveAutoGeneration`) is off. Noise and triangle waves are not configured.
