# I2SAdapter

Abstract adapter for I2S (Inter-IC Sound) audio peripherals. Supports master/slave modes, standard audio formats (Philips, MSB, LSB, PCM), multiple data widths, and DMA-based circular streaming for continuous audio playback/capture.

Header: `#include <Adapter/I2SAdapter.h>`

## Template Parameter

```cpp
template<typename HandleType>
class I2SAdapter : public IAdapter;
```

## Option Types

| Type | Base | Description |
|------|------|-------------|
| `ClockSourceOption` | `IOption<uint32>` | Audio clock source |
| `SlotCountOption` | `IOption<uint8>` | Number of TDM slots |
| `SlotOption` | `IOptionFlag<uint32>` | Active TDM slot bitmask |

## Parameters

```cpp
struct Parameters {
    Mode mode = Mode::Master;
    Direction direction = Direction::Tx;
    Standard standard = Standard::Philips;
    DataFormat dataFormat = DataFormat::B16;
    ClockPolarity clockPolarity = ClockPolarity::Low;
    ClockSourceOption clockSource;
    uint32 sampleRateHz = 44100;
    bool mclkOutput = true;
    SlotCountOption slotCount;
    SlotOption activeSlots;
};
```

| Field | Options | Default | Description |
|-------|---------|---------|-------------|
| `mode` | `Master`, `Slave` | `Master` | I2S clock role |
| `direction` | `Tx`, `Rx`, `TxRx` | `Tx` | Audio data direction |
| `standard` | `Philips`, `MSB`, `LSB`, `PCMShort`, `PCMLong` | `Philips` | Audio protocol |
| `dataFormat` | `B16`, `B16Extended`, `B24`, `B32` | `B16` | Sample bit depth |
| `sampleRateHz` | `uint32` | `44100` | Sample rate |
| `mclkOutput` | `bool` | `true` | Enable master clock output |

## Enums

| Enum | Values | Description |
|------|--------|-------------|
| `Irq` | `TxComplete`, `TxHalfComplete`, `RxComplete`, `RxHalfComplete` | DMA interrupt points |
| `Error` | `None`, `Overrun`, `Underrun`, `FrameError` | Audio errors |

## API

### Synchronous

| Method | Return | Description |
|--------|--------|-------------|
| `Transmit(uint16* buffer, uint32 size)` | `ResultStatus` | Blocking audio transmit |
| `Receive(uint16* buffer, uint32 size)` | `ResultStatus` | Blocking audio receive |
| `TransmitReceive(uint16* tx, uint16* rx, uint32 size)` | `ResultStatus` | Blocking full-duplex |

### Asynchronous (DMA/interrupt)

| Method | Return | Description |
|--------|--------|-------------|
| `TransmitAsync(uint16* buffer, uint32 size)` | `ResultStatus` | Non-blocking transmit |
| `ReceiveAsync(uint16* buffer, uint32 size)` | `ResultStatus` | Non-blocking receive |
| `TransmitReceiveAsync(uint16* tx, uint16* rx, uint32 size)` | `ResultStatus` | Non-blocking full-duplex |

### Circular (continuous streaming)

| Method | Return | Description |
|--------|--------|-------------|
| `TransmitCircular(uint16* buffer, uint32 size)` | `ResultStatus` | Continuous DMA transmit (double-buffer) |
| `ReceiveCircular(uint16* buffer, uint32 size)` | `ResultStatus` | Continuous DMA receive |

### Control

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(Parameters val)` | `ResultStatus` | Apply I2S configuration |
| `SetTimeout(uint32 val)` | `void` | Set timeout for sync operations (ms) |
| `StopTransmit()` | `ResultStatus` | Stop transmit |
| `StopReceive()` | `ResultStatus` | Stop receive |
| `IrqHandler()` | `void` | Call from I2S/DMA IRQ |

## Callbacks

| Callback | Type | Description |
|----------|------|-------------|
| `onInterrupt` | `std::function<void(Irq)>` | DMA transfer event |
| `onError` | `std::function<void(Error)>` | Audio error |

## BSP Configuration Example

```cpp
// BSP.h
class BSP {
public:
    static AI2S i2s2;
};

// BSP.cpp
AI2S BSP::i2s2 = { SPI2, SystemCoreClock }; // I2S often shares SPI peripheral

void BSP::InitAdapterPeripheryEvents() {
    i2s2.beforePeripheryInit = []() {
        LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_SPI2);

        AGPIO::AlternateInit({ GPIOB, 12, 0 }); // WS
        AGPIO::AlternateInit({ GPIOB, 13, 0 }); // CK
        AGPIO::AlternateInit({ GPIOB, 15, 0 }); // SD

        NVIC_SetPriority(SPI2_IRQn, 1);
        NVIC_EnableIRQ(SPI2_IRQn);

        return ResultStatus::ok;
    };
}

// DeviceIrq.cpp
void SPI2_IRQHandler() {
    BSP::i2s2.IrqHandler();
}
```

## Usage Example

```cpp
BSP::i2s2.SetParameters({
    .mode = AI2S::Mode::Master,
    .direction = AI2S::Direction::Tx,
    .standard = AI2S::Standard::Philips,
    .dataFormat = AI2S::DataFormat::B16,
    .sampleRateHz = 48000,
    .mclkOutput = true
});

// Continuous audio playback using circular DMA
uint16 audioBuffer[1024]; // double-buffer: first half plays while second half fills
BSP::i2s2.onInterrupt = [](AI2S::Irq irq) {
    if (irq == AI2S::Irq::TxHalfComplete) {
        // Fill first half of audioBuffer
    } else if (irq == AI2S::Irq::TxComplete) {
        // Fill second half of audioBuffer
    }
};
BSP::i2s2.TransmitCircular(audioBuffer, 1024);
```
