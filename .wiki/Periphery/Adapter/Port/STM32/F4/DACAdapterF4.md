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

The underrun flag is reported whether or not its interrupt is enabled. `WriteContinuous()` enables the interrupt of the channel, and `StopDMA()` disables it.

## Continuous output

`WriteContinuous(buffer, count)` starts the DMA attached with `SetDMA()`. `count` is the number of 16-bit samples, and the DMA moves them to the 12-bit right-aligned holding register of the channel. `StopDMA()` stops the attached DMA, whatever it is, and clears the DMA request, the trigger and the underrun interrupt of the channel.

`WriteContinuous()` needs a hardware trigger in `triggerSource`: a timer or an external line. The default, software, is refused with `invalidParameter`, because a software trigger makes one DMA request per write and does not clock a continuous output.

The channel needs a hardware trigger for this, so the example selects TIM6. The application configures TIM6 so that it runs and sends its trigger output.

```cpp
ADMA dma1str5 = { DMA1, 5 };

dma1str5.beforePeripheryInit = []() {
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);
    NVIC_EnableIRQ(DMA1_Stream5_IRQn);
    return ResultStatus::ok;
};

dma1str5.SetParameters({
    .peripheral = ADMA::Peripheral::Channel7,
    .direction = ADMA::Direction::MemoryToPeripheral,
    .addressMode = ADMA::AddressMode::IncrementingToFixed,
    .circularMode = true,
    .enableTransferCompleteIT = false,
    .dataWidth = ADMA::DataWidth::B16
});

ADAC dac1ch1 = { DAC, 1 };

ADAC::Parameters parameters;
parameters.triggerSource = ADAC::TriggerSource::Timer6Trigger;
dac1ch1.SetParameters(parameters);

dac1ch1.SetDMA(&dma1str5);
dac1ch1.WriteContinuous(samples, 1024);
```

The DMA must be the stream and request that RM0090 wires to the channel (DMA1 request mapping):

| DAC channel | DMA | Stream | Request |
|-------------|-----|--------|---------|
| 1 | DMA1 | 5 | `ADMA::Peripheral::Channel7` |
| 2 | DMA1 | 6 | `ADMA::Peripheral::Channel7` |

Before it starts the DMA, the adapter checks the attached DMA:

| Check | Required value |
|-------|----------------|
| Stream | Stream 5 of DMA1 for channel 1, stream 6 of DMA1 for channel 2 |
| Request | `ADMA::Peripheral::Channel7` |
| Direction | `MemoryToPeripheral` |
| Address mode | `IncrementingToFixed`: the memory side increments and the holding register stays fixed |
| Peripheral width | 16 bits |
| Memory width | 16 bits |

A DMA that does not match is refused with `invalidParameter`, and the channel is not started. The adapter does not configure the DMA. The application sets it with the DMA adapter's `SetParameters()`, enables the DMA clock, and enables the stream's NVIC line. Set `circularMode` for a loop; in normal mode the output stops at the end of the buffer.

| Call | Result |
|------|--------|
| `WriteContinuous()` with no DMA attached | `notAvailable` |
| `WriteContinuous()` with the default trigger or `TriggerSource::Software` | `invalidParameter` |
| `WriteContinuous()` with a DMA that does not match the table above | `invalidParameter` |
| `WriteContinuous()` with a count of 0, or above 65535 | `invalidParameter` (from the DMA adapter) |
| `WriteContinuous()` while the stream runs | `busy` (from the DMA adapter) |
| `StopDMA()` with no DMA attached | `notAvailable` |

## Not supported

- Triggers from TIM3 and TIM1 are not in the F4 DAC's trigger table.
- The wave generator (`WaveAutoGeneration`) is off. Noise and triangle waves are not configured.
