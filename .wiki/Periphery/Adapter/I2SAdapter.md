# I2SAdapter

Abstract adapter for I2S (Inter-IC Sound) audio peripherals. Supports master/slave modes, standard audio formats (Philips, MSB, LSB, PCM, PDM), multiple data widths, and DMA-based circular streaming for continuous audio playback/capture.

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
| `SlotCountOption` | `IOption<uint8>` | Number of slots |
| `SlotOption` | `IOptionFlag<uint32>` | Active slot bitmask |

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
| `standard` | `Philips`, `MSB`, `LSB`, `PCMShort`, `PCMLong`, `PDM` | `Philips` | Audio protocol (`PDM` for PDM microphone input) |
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

## ESP32 Port

`AI2S` is `I2SAdapterESP` (`Periphery/Adapter/Port/ESP32/Common/I2SAdapterESP.h`), built on the ESP-IDF I2S channel driver. `HandleType` is `void`: the peripheral is selected by an `i2s_port_t`.

Header: `#include <Adapter/Port/ESP32/Common/I2SAdapterESP.h>`

### Constructors

```cpp
I2SAdapterESP();
I2SAdapterESP(i2s_port_t port, GpioConfig gpio);
I2SAdapterESP(i2s_port_t port, GpioConfig gpio, DmaConfig dma);
```

### GpioConfig

```cpp
struct GpioConfig {
    int mclk = -1;
    int bclk = -1;
    int ws = -1;
    int dataOut = -1;
    int dataIn = -1;
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `mclk` | `-1` | Master clock output (standard and TDM modes, when `mclkOutput` is `true`) |
| `bclk` | `-1` | Bit clock. In PDM mode, the PDM clock when set |
| `ws` | `-1` | Word select. In PDM mode, the PDM clock when `bclk` is `-1` |
| `dataOut` | `-1` | Data output (Tx) |
| `dataIn` | `-1` | Data input (Rx) |

A value of `-1` leaves the signal unused.

### DmaConfig

```cpp
struct DmaConfig {
    uint32 bufferCount = 6;
    uint32 frameCount = 240;
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `bufferCount` | `6` | Number of DMA descriptors (ESP-IDF `dma_desc_num`) |
| `frameCount` | `240` | Frames per DMA buffer (ESP-IDF `dma_frame_num`) |

### Mode Selection

`SetParameters()` picks the driver mode from `Parameters`:

- `standard == Standard::PDM`: PDM mode.
- `slotCount` above 2: TDM mode. Requires a chip with TDM support; otherwise `notSupported`.
- Otherwise: standard mode (`Philips`, `MSB`, `PCMShort`, `PCMLong`, and `LSB` where supported).

`SetParameters()` returns `ResultStatus::notSupported` when:

- `standard` is `PDM` and `dataFormat` is not `B16`, or `direction` is `TxRx`.
- `standard` is `PDM` and the chip lacks PDM support for the requested direction.
- `slotCount` is above 2 on a chip without TDM support.
- `standard` is `PDM` and the port is neither `I2S_NUM_0` nor `I2S_NUM_AUTO`.
- `standard` is `LSB` on the classic ESP32 (`SOC_I2S_HW_VERSION_1`), or together with `slotCount` above 2 on any chip.

These checks run before the previous channels are deleted, so a request rejected by them leaves the current configuration running. A failure inside the driver after that point returns `error` and leaves the adapter without channels.

### PDM Mode

- Clock: `gpio.bclk` when it is set, otherwise `gpio.ws`. In master mode the clock pin is an output.
- Data: `gpio.dataIn` for `Direction::Rx`, `gpio.dataOut` for `Direction::Tx`.
- Only `DataFormat::B16` and a single direction (`Rx` or `Tx`) per adapter.
- Only on `I2S_NUM_0`. On other ports `SetParameters()` returns `ResultStatus::notSupported`.
- `activeSlots` on receive: `Slot::Left` selects the left slot, `Slot::Right` the right slot, and both select stereo. When neither is set, the ESP-IDF default applies (left for mono, both for stereo). On transmit the selection is applied only on the classic ESP32 (`SOC_I2S_HW_VERSION_1`); other chips use the ESP-IDF default.
- `slotCount` of 1 or less selects mono; anything above 1 selects stereo.
- `clockPolarity` `High` inverts the PDM clock.
- `sampleRateHz` sets the PDM clock. `mclkOutput` and `mclk` are not used.
- Received samples are 16-bit PCM from the hardware PDM-to-PCM filter (the ESP-IDF default for this chip). Transmitted 16-bit PCM is converted to PDM by the hardware.

### Option Constants

`ClockSource`:

| Constant | Value | Availability |
|----------|-------|--------------|
| `Default` | `I2S_CLK_SRC_DEFAULT` | All chips |
| `Apll` | `I2S_CLK_SRC_APLL` | Chips with `SOC_I2S_SUPPORTS_APLL` (classic ESP32 included) |

An unset `clockSource` (`0`) also selects the default clock.

`SlotCount`: `S1`, `S2`, `S4`, `S8`, `S16` (1, 2, 4, 8, 16 slots). Values above 2 need TDM support.

`Slot`:

| Constant | Description |
|----------|-------------|
| `S0` to `S15` | Single slot bit (TDM slot 0 to 15). Standard mode ignores bits other than `Left` and `Right`. Bits above 1 take effect only with TDM. |
| `Left` | Slot 0 (bit 0). Standard and PDM mode |
| `Right` | Slot 1 (bit 1). Standard and PDM mode |
| `Mono` | Same as `S0` (`Left`) |
| `Stereo` | `S0` and `S1` (`Left` and `Right`) |

### Transfer API

| Method | Return | Description |
|--------|--------|-------------|
| `Transmit(uint16 *buffer, uint32 size)` | `ResultStatus` | Blocking write of `size` 16-bit samples. `ok` when all are written, `timeout` when fewer are written |
| `Receive(uint16 *buffer, uint32 size)` | `ResultStatus` | Blocking read of `size` 16-bit samples. `ok` when all are read, `timeout` when fewer are read |
| `TransmitBytes(const uint8 *buffer, uint32 size)` | `Result<uint32>` | Blocking write of `size` bytes. Value: bytes written, which is less than `size` on timeout |
| `ReceiveBytes(uint8 *buffer, uint32 size)` | `Result<uint32>` | Blocking read of `size` bytes. Value: bytes read, which is less than `size` on timeout |
| `StopTransmit()` | `ResultStatus` | Disable the TX channel. `noInit` if there is no TX channel |
| `StopReceive()` | `ResultStatus` | Disable the RX channel. `noInit` if there is no RX channel |
| `SetTimeout(uint32 val)` | `void` | Timeout for `Transmit`, `Receive`, `TransmitBytes` and `ReceiveBytes`, in milliseconds (default `1000`) |

- `TransmitBytes()` and `ReceiveBytes()` return `noInit` when the channel does not exist, `timeout` when the transfer times out with no bytes moved, and `error` on a driver failure with no bytes moved.
- `Transmit()` and `Receive()` return the same statuses as the byte-based methods on errors.
- A transfer after `StopTransmit()` or `StopReceive()` enables the channel again before it runs.
- `TransmitBytes()` and `ReceiveBytes()` are port-specific and are not part of the generic interface.

Returns `ResultStatus::notSupported`: `TransmitReceive`, `TransmitAsync`, `ReceiveAsync`, `TransmitReceiveAsync`, `TransmitCircular`, `ReceiveCircular`.

`IrqHandler()` does nothing, and `onInterrupt` and `onError` are not called on this port.

### PDM Microphone Example

PDM clock on GPIO0 and data on GPIO34 of a classic ESP32, 16 kHz mono:

```cpp
AI2S mic = { I2S_NUM_0, { .bclk = 0, .dataIn = 34 } };

mic.SetParameters({
    .mode = AI2S::Mode::Master,
    .direction = AI2S::Direction::Rx,
    .standard = AI2S::Standard::PDM,
    .dataFormat = AI2S::DataFormat::B16,
    .sampleRateHz = 16000,
    .slotCount = AI2S::SlotCount::S1,
    .activeSlots = AI2S::Slot::Mono
});

// Read 256 samples (512 bytes); timeout set in milliseconds
uint16 samples[256];
mic.SetTimeout(500);
if (mic.Receive(samples, 256) == ResultStatus::ok) {
    // samples[0..255] hold 16-bit PCM
}
```

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
