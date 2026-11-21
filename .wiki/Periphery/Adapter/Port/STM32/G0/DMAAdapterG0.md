# DMAAdapterG0

The [DMA adapter](../../../DMAAdapter.md) on the STM32G0 DMA1 controller. Each channel is one `DMAAdapterG0` object. The request comes from the DMAMUX (`LL_DMA_SetPeriphRequest`), so the request names in `DMAAdapterG0::Peripheral` select the peripheral.

Header: `#include <Adapter/Port/STM32/G0/DMAAdapterG0.h>`

Alias: `ADMA`. The constructor takes the controller and the 1-based channel: `ADMA dma1ch1 = { DMA1, 1 };`.

Enabled by `#define VHAL_STM32_G0_DMA`, which `Periphery/Adapter/Port/STM32/Adapter.h` checks before it includes the port. The BSP enables the DMA clock and the NVIC line before the first `SetParameters()`, and forwards the channel interrupt to `IrqHandler()`.

## Channels

The port has seven channels, numbered 1 to 7, as on the G07x and G0Bx. A channel outside 1 to 7 is refused with `invalidParameter`. The G030, G031 and G041 have five channels and lack the DMAMUX request names this port uses, so the port does not build for them.

## Parameters

| Field | Port behaviour |
|-------|----------------|
| `peripheral` | Written to the DMAMUX request of the channel |
| `dataWidth` | The memory width |
| `periphDataWidth` | The peripheral width. `0` means the same as `dataWidth` |
| `priority` | `0` to `3`. Any other value is refused with `invalidParameter`; it is never rounded down |
| `circularMode` | Circular mode. Completion never ends the run |
| `enableTransferCompleteIT` | Enables the transfer-complete interrupt |

## Transfers

- `StartTransfer()` takes the byte size of the buffer and divides it by the item width, which is `dataWidth`. The peripheral width must equal `dataWidth`: a start with `periphDataWidth` set to another width is refused with `invalidParameter`. A size of zero, a size that is not a whole number of items, and a count above 65535 are refused with `invalidParameter`.
- The half-transfer interrupt is enabled when `onHalfTransfer` is set when the transfer starts, and not otherwise. The transfer-error interrupt is always enabled.
- A start writes the stored parameters to the channel without calling the hooks. `Initialization()`, which `SetParameters()` calls, runs `beforePeripheryInit` and `afterPeripheryInit`. A start does not run them again.
- `Stop()` disables the channel and its interrupts. A later `Start()` begins a new run.

## Status

`GetStatus()` reads the completion and the error latched by the interrupt, so polling works with the interrupt enabled.

| Result | When |
|--------|------|
| `error` | A transfer error is latched or the error flag is set |
| `ok` (normal mode) | The completion is latched or the complete flag is set |
| `busy` (normal mode) | The channel runs and the transfer has not completed |
| `stop` (normal mode) | The channel is off and the transfer did not complete, which is the state after `Stop()` |
| `ok` (circular mode) | The channel runs. Circular mode never completes |
| `stop` (circular mode) | The channel is off, which is the state after `Stop()` |
| `invalidParameter` | The channel is not on this chip |

## Callbacks

| Callback | When |
|----------|------|
| `onHalfTransfer` | Half of the buffer is moved. Normal mode calls it once per run, circular mode for each half |
| `onTransferComplete` | The run completed. The run ends in normal mode |
| `onError` | A transfer error. The channel is disabled and the run ends |

A channel stopped with `Stop()` raises its flags while it drains. The adapter clears them and calls no callback.

## Refused

| Call | Result | Reason |
|------|--------|--------|
| `SetParameters()` while the channel runs | `busy` | The control register is write-protected while the channel is enabled. The parameters are stored and written by the next call. The run keeps the run mode it started with |
| `StartTransfer()` while the channel runs | `busy` | A running transfer is not aborted |
| Peripheral width different from `dataWidth` | `invalidParameter` | The counter is defined for equal widths only |
| Priority above 3 | `invalidParameter` | Not rounded |
| Channel not on the chip | `invalidParameter` | No register access |
| Size zero, a partial item, or a count above 65535 | `invalidParameter` | The transfer counter is 16 bits |

## Usage

```cpp
ADMA dma1ch1 = { DMA1, 1 };

dma1ch1.beforePeripheryInit = []() {
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);
    NVIC_SetPriority(DMA1_Channel1_IRQn, 0);
    NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    return ResultStatus::ok;
};

dma1ch1.SetParameters({
    .peripheral = ADMA::Peripheral::Adc1,
    .direction = ADMA::Direction::PeripheralToMemory,
    .addressMode = ADMA::AddressMode::FixedToIncrementing,
    .circularMode = true,
    .dataWidth = ADMA::DataWidth::B16
});
```
