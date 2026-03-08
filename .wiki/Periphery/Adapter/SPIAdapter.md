# SPIAdapter

Abstract adapter for SPI peripherals. Supports master/slave mode, configurable clock polarity/phase, automatic chip select management, and full-duplex synchronous and asynchronous transfers.

Header: `#include <Adapter/SPIAdapter.h>`

## Template Parameter

```cpp
template<typename HandleType>
class SPIAdapter : public IAdapter;
```

`HandleType` — platform-specific SPI register structure.

## Parameters

```cpp
struct Parameters {
    Mode mode = Mode::Master;
    Direction direction = Direction::TxRx;
    ClockPolarity clockPolarity = ClockPolarity::Low;
    ClockPhase clockPhase = ClockPhase::Edge1;
    FirstBit firstBit = FirstBit::MSB;
    uint32 maxSpeedHz = 100;
    GPIOAdapter *chipSelectPin = nullptr;
    ChipSelect chipSelect = ChipSelect::Low;
};
```

| Field | Options | Default | Description |
|-------|---------|---------|-------------|
| `mode` | `Master`, `Slave` | `Master` | SPI role |
| `direction` | `Tx`, `Rx`, `TxRx` | `TxRx` | Transfer direction |
| `clockPolarity` | `Low`, `High` | `Low` | Clock idle state (CPOL) |
| `clockPhase` | `Edge1`, `Edge2` | `Edge1` | Data sampling edge (CPHA) |
| `firstBit` | `MSB`, `LSB` | `MSB` | Bit transmission order |
| `maxSpeedHz` | any `uint32` | `100` | Maximum SPI clock frequency |
| `chipSelectPin` | `GPIOAdapter*` | `nullptr` | GPIO pin for software CS (null = hardware CS) |
| `chipSelect` | `Low`, `High` | `Low` | Active level for chip select |

## Enums

| Enum | Values | Description |
|------|--------|-------------|
| `Irq` | `Tx`, `Rx`, `TxRx` | Interrupt source |
| `Error` | `None`, `Overrun`, `ModeFault`, `FrameFormat` | Error types |

## Synchronous API

| Method | Return | Description |
|--------|--------|-------------|
| `Write(DataType data)` | `ResultStatus` | Write a single value (auto CS) |
| `WriteArray(DataType* buffer, uint32 size)` | `ResultStatus` | Write an array (auto CS) |
| `Read(uint32 size)` | `Result<DataType>` | Read a single value (auto CS) |
| `ReadArray(DataType* buffer, uint32 size)` | `ResultStatus` | Read an array (auto CS) |
| `WriteRead(DataType data)` | `Result<DataType>` | Full-duplex single value (auto CS) |
| `WriteReadArray(DataType* tx, DataType* rx, uint32 size)` | `ResultStatus` | Full-duplex array (auto CS) |

## Asynchronous API

| Method | Return | Description |
|--------|--------|-------------|
| `WriteAsync(DataType& data)` | `ResultStatus` | Non-blocking write (CS asserted, release manually) |
| `WriteArrayAsync(DataType* buffer, uint32 size)` | `ResultStatus` | Non-blocking array write |
| `ReadAsync(DataType* buffer)` | `ResultStatus` | Non-blocking read |
| `ReadArrayAsync(DataType* buffer, uint32 size)` | `ResultStatus` | Non-blocking array read |
| `WriteReadAsync(DataType* tx, DataType* rx)` | `ResultStatus` | Non-blocking full-duplex |
| `WriteReadArrayAsync(DataType* tx, DataType* rx, uint32 size)` | `ResultStatus` | Non-blocking full-duplex array |

## Configuration & Control

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(Parameters val)` | `Result<uint32>` | Apply config. Returns actual clock prescaler value |
| `SetTimeout(uint32 val)` | `void` | Set timeout for sync operations (ms) |
| `Continuous(bool enable)` | `void` | Keep CS asserted across multiple transfers |
| `ChipSelect(bool isSelect)` | `void` | Manually assert/release CS |
| `IrqHandler()` | `void` | Must be called from SPI IRQ handler |
| `AbortReceive()` | `void` | Abort active receive |
| `AbortTransmit()` | `void` | Abort active transmit |
| `AbortAll()` | `void` | Abort all operations |

## Callbacks

| Callback | Type | Description |
|----------|------|-------------|
| `onInterrupt` | `std::function<void(Irq)>` | Called on async TX/RX completion |
| `onError` | `std::function<void(Error)>` | Called on SPI error |

## BSP Configuration Example

```cpp
// BSP.h
class BSP {
public:
    static ASPI spi1;
    static AGPIO spiCS;
};

// BSP.cpp
ASPI BSP::spi1 = { SPI1, SystemCoreClock };
AGPIO BSP::spiCS = { GPIOA, 4 };

void BSP::InitAdapterPeripheryEvents() {
    spi1.beforePeripheryInit = []() {
        LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SPI1);

        AGPIO::AlternateInit({ GPIOA, 5, 0, AGPIO::Pull::None, AGPIO::Speed::VeryHigh }); // SCK
        AGPIO::AlternateInit({ GPIOA, 6, 0, AGPIO::Pull::None, AGPIO::Speed::VeryHigh }); // MISO
        AGPIO::AlternateInit({ GPIOA, 7, 0, AGPIO::Pull::None, AGPIO::Speed::VeryHigh }); // MOSI

        NVIC_SetPriority(SPI1_IRQn, 1);
        NVIC_EnableIRQ(SPI1_IRQn);

        return ResultStatus::ok;
    };
}

// DeviceIrq.cpp
void SPI1_IRQHandler() {
    BSP::spi1.IrqHandler();
}
```

## Usage Example

```cpp
// Configure with software chip select
BSP::spi1.SetParameters({
    .mode = ASPI::Mode::Master,
    .clockPolarity = ASPI::ClockPolarity::Low,
    .clockPhase = ASPI::ClockPhase::Edge1,
    .maxSpeedHz = 1000000,
    .chipSelectPin = &BSP::spiCS,
    .chipSelect = ASPI::ChipSelect::Low
});

// Write-read (CS auto-managed)
auto result = BSP::spi1.WriteRead<uint8>(0x75);
if (result.IsOk()) {
    uint8 whoAmI = result.Value();
}

// Multi-transfer with continuous CS
BSP::spi1.Continuous(true);
BSP::spi1.Write<uint8>(0x80 | registerAddr);
auto data = BSP::spi1.Read<uint8>();
BSP::spi1.Continuous(false);
```
