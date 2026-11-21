# DACAdapterG4

The [DAC adapter](../../../DACAdapter.md) on the STM32G4 DAC channels. The adapter is `DACAdapter<DAC_TypeDef, ADMA>`, so the DMA is the `DMAAdapterG4` attached with `SetDMA()`.

Header: `#include <Adapter/Port/STM32/G4/DACAdapterG4.h>`

Alias: `ADAC`. The constructor takes the DAC instance and the channel: `ADAC dac1ch1 = { DAC1, 1 };`.

Enabled by `#define VHAL_STM32_G4_DAC`, which `Periphery/Adapter/Port/STM32/Adapter.h` checks before it includes the port.

## Continuous Output

`WriteContinuous(uint16* buffer, uint32 count)` starts the DMA. `count` is the number of 16-bit samples. The DMA moves them to the data holding register, 12 bits right aligned.

Before it starts the DMA, the adapter checks the attached DMA with `CheckPeripheral()`:

| Check | Required value |
|-------|----------------|
| Request | The DMAMUX request of this channel: `Dac1Channel1`/`Dac1Channel2` on DAC1, `Dac2Channel1` on DAC2 channel 1, `Dac3Channel1`/`Dac3Channel2` on DAC3 and `Dac4Channel1`/`Dac4Channel2` on DAC4, where the chip has them |
| Direction | `MemoryToPeripheral` |
| Peripheral width | 16 bits |
| Memory width | 16 bits |

A DMA that does not match is refused with `invalidParameter`, and the DAC is not started. A byte-width DMA is refused, so the 16-bit count is never doubled into twice the transfers. A DAC instance or channel with no request in the table is refused with `invalidParameter`.

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
