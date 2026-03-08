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
| `priority` | `uint32` | `0` | DMA channel priority |

## API

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(const Parameters&)` | `ResultStatus` | Apply DMA configuration |
| `Start<T>(const T* from, T* to, uint32 count)` | `ResultStatus` | Start a typed transfer |
| `Stop()` | `ResultStatus` | Stop active transfer |
| `GetStatus()` | `ResultStatus` | Get current transfer state |
| `GetLastTransferSize()` | `uint32` | Get size of the last completed transfer |
| `IrqHandler()` | `void` | Call from DMA channel IRQ |

## Callbacks

| Callback | Type | Description |
|----------|------|-------------|
| `onTransferComplete` | `std::function<void()>` | Full transfer completed |
| `onHalfTransfer` | `std::function<void()>` | Half transfer reached |
| `onError` | `std::function<void()>` | Transfer error |

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
