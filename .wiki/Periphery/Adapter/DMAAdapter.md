# DMAAdapter

Abstract adapter for DMA (Direct Memory Access) controllers. Provides memory-to-peripheral, peripheral-to-memory, and memory-to-memory transfers with configurable data width, priority, and circular mode.

Header: `#include <Adapter/DMAAdapter.h>`

## Template Parameter

```cpp
template<typename HandleType>
class DMAAdapter : public IAdapter;
```

## Option Types

| Type | Base | Description |
|------|------|-------------|
| `DMAPeripheralOption` | `IOption<uint32>` | Peripheral request source (platform-specific) |

## Parameters

```cpp
struct Parameters {
    DMAPeripheralOption peripheral;
    Direction direction;
    AddressMode addressMode;
    bool circularMode = false;
    bool enableTransferCompleteIT = true;
    DataWidth dataWidth = DataWidth::B8;
    DataWidth periphDataWidth = static_cast<DataWidth>(0);
    uint32 priority = 0;
};
```

| Field | Options | Default | Description |
|-------|---------|---------|-------------|
| `peripheral` | `DMAPeripheralOption` | — | Which peripheral triggers DMA requests |
| `direction` | `MemoryToPeripheral`, `PeripheralToMemory`, `MemoryToMemory` | — | Transfer direction |
| `addressMode` | `FixedToIncrementing`, `IncrementingToFixed`, `IncrementingToIncrementing` | — | Address increment behavior |
| `circularMode` | `bool` | `false` | Auto-restart when transfer completes |
| `enableTransferCompleteIT` | `bool` | `true` | Interrupt on transfer complete |
| `dataWidth` | `B8`, `B16`, `B32` | `B8` | Transfer unit size |
| `periphDataWidth` | `0`, `B8`, `B16`, `B32` | `0` | Peripheral-side transfer width; `0` uses `dataWidth` where supported |
| `priority` | `uint32` | `0` | DMA channel priority |

The STM32 F4, G0 and G4 DMA ports honour this field. Other ports may ignore it.

## API

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(const Parameters&)` | `ResultStatus` | Apply DMA configuration |
| `Start<T>(const T* from, T* to, uint32 count)` | `ResultStatus` | Start a typed transfer |
| `Stop()` | `ResultStatus` | Stop active transfer |
| `GetStatus()` | `ResultStatus` | Get current transfer state |
| `GetLastTransferSize()` | `uint32` | Get size of the last completed transfer |
| `IrqHandler()` | `void` | Call from DMA channel IRQ |
| `GetParameters()` | `const Parameters&` | The stored parameters |
| `GetPeriphDataWidth()` | `DataWidth` | The peripheral width: `periphDataWidth`, or `dataWidth` when it is `0` |
| `CheckWidths()` | `ResultStatus` | `ok` when the peripheral width equals the memory width. Otherwise `invalidParameter`. A port refuses a start that fails it |
| `IsRunCircular()` | `bool` | The circular mode of the transfer that was started last. The interrupt and the status read this, not the stored parameters |
| `CheckPeripheral(uint32 request, Direction, DataWidth)` | `ResultStatus` | `ok` when the request, the direction and both widths match. Otherwise `invalidParameter` |
| `ItemCount(uint32 bytes, uint32 itemBytes, uint32 maxCount, uint32& count)` | `ResultStatus` | Static. Items in a size. `invalidParameter` for zero, a partial item or a count above `maxCount` |
| `LatchedStatus(bool error, bool complete, bool circular, bool channelEnabled)` | `ResultStatus` | Static. The status rules of the ports, see below |

`CheckPeripheral()` is for the data adapters. A DAC or ADC attached to a DMA calls it before it starts the DMA, so a DMA that does not carry the request of the peripheral, or that moves another width, is refused.

## Callbacks

| Callback | Type | Description |
|----------|------|-------------|
| `onTransferComplete` | `std::function<void()>` | Full transfer completed |
| `onHalfTransfer` | `std::function<void()>` | Half transfer reached |
| `onError` | `std::function<void()>` | Transfer error |

Replace a callback while its interrupt may run with its setter: `SetOnTransferComplete(handler)`, `SetOnHalfTransfer(handler)` and `SetOnError(handler)`. See [Changing a callback while its interrupt may run](IAdapter.md#changing-a-callback-while-its-interrupt-may-run).

## Status Rules

The STM32 G0 and G4 ports latch the completion and the error in software, so `GetStatus()` gives the right result after the interrupt has cleared the flags. `LatchedStatus()` holds the rule:

| Result | Normal mode | Circular mode |
|--------|-------------|---------------|
| `error` | A transfer error is latched or flagged | A transfer error is latched or flagged |
| `ok` | The completion is latched or flagged | The channel runs |
| `busy` | The channel runs and the transfer has not completed | Not returned |
| `stop` | The channel is off and the transfer did not complete, as after `Stop()` | The channel is off, as after `Stop()` |

## Transfer Count

The transfer counter counts items of the width both sides share. `Start<T>()` passes `sizeof(T) * count` bytes, and the port divides them by the item size. A start with a peripheral width that differs from the memory width is refused with `invalidParameter`, because a count of one width is not a count of the other. A size of zero, a size that is not a whole number of items and a count above 65535 are refused with `invalidParameter`. A caller that moves 16-bit samples into a byte-width DMA gets twice the transfers, so a data adapter checks the width with `CheckPeripheral()` before it starts.

The run mode is latched when a transfer starts. A `SetParameters()` during a run returns `busy` and changes only the stored parameters, so the run still ends the way it started. The next start uses the stored parameters.

## Refused on STM32 G0 and G4

| Call | Result |
|------|--------|
| `SetParameters()` or `Initialization()` while the channel runs | `busy` |
| `Start()` while the channel runs | `busy`. The running transfer is not aborted |
| `Start()` with a peripheral width that differs from the memory width | `invalidParameter` |
| Priority above 3 | `invalidParameter` |
| Channel not on the chip | `invalidParameter` |

The port pages are [G0](Port/STM32/G0/DMAAdapterG0.md) and [G4](Port/STM32/G4/DMAAdapterG4.md).

## BSP Configuration Example

```cpp
// BSP.h
class BSP {
public:
    static ADMA dma1ch1;
};

// BSP.cpp
ADMA BSP::dma1ch1 = { DMA1, 1 }; // DMA1, channel 1

void BSP::InitAdapterPeripheryEvents() {
    dma1ch1.beforePeripheryInit = []() {
        LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);

        NVIC_SetPriority(DMA1_Channel1_IRQn, 0);
        NVIC_EnableIRQ(DMA1_Channel1_IRQn);

        return ResultStatus::ok;
    };
}

// DeviceIrq.cpp
void DMA1_Channel1_IRQHandler() {
    BSP::dma1ch1.IrqHandler();
}
```

## Usage Example

```cpp
BSP::dma1ch1.SetParameters({
    .peripheral = ADMA::Peripheral::ADC,
    .direction = ADMA::Direction::PeripheralToMemory,
    .addressMode = ADMA::AddressMode::FixedToIncrementing,
    .circularMode = true,
    .dataWidth = ADMA::DataWidth::B16
});

// Start circular ADC DMA transfer
uint16 adcBuffer[4];
BSP::dma1ch1.Start(adcRegisterAddress, adcBuffer, 4);

BSP::dma1ch1.onTransferComplete = []() {
    // Process adcBuffer
};
```
