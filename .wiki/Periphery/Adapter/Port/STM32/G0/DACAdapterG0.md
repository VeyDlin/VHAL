# DACAdapterG0

The [DAC adapter](../../../DACAdapter.md) on the STM32G0 DAC1, channels 1 and 2. The adapter is `DACAdapter<DAC_TypeDef, ADMA>`, so the DMA is the `DMAAdapterG0` attached with `SetDMA()`.

Header: `#include <Adapter/Port/STM32/G0/DACAdapterG0.h>`

Alias: `ADAC`. The constructor takes the DAC instance and the channel: `ADAC dac1ch1 = { DAC1, 1 };`.

Enabled by `#define VHAL_STM32_G0_DAC`, which `Periphery/Adapter/Port/STM32/Adapter.h` checks before it includes the port.

## Continuous Output

`WriteContinuous(uint16* buffer, uint32 count)` starts the DMA. `count` is the number of 16-bit samples. The DMA moves them to the data holding register, 12 bits right aligned.

Before it starts the DMA, the adapter checks the attached DMA with `CheckPeripheral()`:

| Check | Required value |
|-------|----------------|
| Request | `Dac1Channel1` for channel 1, `Dac1Channel2` for channel 2 |
| Direction | `MemoryToPeripheral` |
| Peripheral width | 16 bits |
| Memory width | 16 bits |

A DMA that does not match is refused with `invalidParameter`, and the DAC is not started. A byte-width DMA is refused, so the 16-bit count is never doubled into twice the transfers.

The adapter does not set the DMAMUX request or the widths. The application configures the DMA with `SetParameters()`.

## Refused

| Call | Result |
|------|--------|
| `WriteContinuous()` with no DMA attached | `notAvailable` |
| `WriteContinuous()` with a DMA that does not match the table above | `invalidParameter` |
| `WriteContinuous()` while the DMA runs | `busy` (from the DMA adapter) |
| `StopDMA()` with no DMA attached | `notAvailable` |

## Errors

A DMA underrun calls `onError` with `Error::DmaUnderrun`. The adapter clears the underrun flag after the call.
