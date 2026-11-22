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
    GPIOAdapter<> *chipSelectPin = nullptr;
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
| `chipSelectPin` | `GPIOAdapter<>*` | `nullptr` | GPIO pin for software CS (null = no software CS control) |
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

### Background Byte Write (generic)

These are the portable asynchronous write. A port that can run a write in the background implements them; the others inherit the defaults below, so existing code compiles unchanged.

```cpp
virtual ResultStatus BeginWriteBytes(const uint8 *buffer, uint32 size);
virtual ResultStatus WaitWriteBytes(uint32 timeoutMs);
virtual ResultStatus CheckDmaBuffer(const uint8 *buffer, uint32 size);
```

| Method | Return | Description |
|--------|--------|-------------|
| `BeginWriteBytes(buffer, size)` | `ResultStatus` | Starts writing `size` bytes without waiting for the bus. Chip select is asserted before the first byte and released after the last one, even when the port splits the buffer. Returns `busy` while an earlier `BeginWriteBytes` has not been collected, `outOfRange` when the buffer needs more transfers than the port queues, and `notSupported` on a port without the capability |
| `WaitWriteBytes(timeoutMs)` | `ResultStatus` | Waits for the write started by `BeginWriteBytes`. Returns `ok` when none is in flight (also when none was started), `timeout` when it is still running. `timeoutMs = 0` only polls. `notSupported` on a port without the capability |
| `CheckDmaBuffer(buffer, size)` | `ResultStatus` | Returns `ok` when the port's bus can read the buffer, and `invalidParameter` when it cannot. The default accepts every buffer |

Rules for a caller:

- Keep the buffer unchanged, and do not free it, until `WaitWriteBytes` returns `ok`.
- Do not start another synchronous transfer on the same bus until the write has been collected. The port waits for the write before a synchronous transfer, but the caller must not depend on that to change a line that the write still uses (for example the ST7789 data/command line).
- Call `CheckDmaBuffer` on every buffer that will be passed to `BeginWriteBytes`, once, at initialisation. Do not rely on a later transfer to fail visibly.
- Synchronous calls (`Write`, `WriteArray`, `Read`, ...) keep their behaviour and can be used on any port.

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

Replace a callback while its interrupt may run with its setter: `SetOnInterrupt(handler)` and `SetOnError(handler)`. See [Changing a callback while its interrupt may run](IAdapter.md#changing-a-callback-while-its-interrupt-may-run).

## ESP32 Port

`ASPI` is `SPIAdapterESP` (`Periphery/Adapter/Port/ESP32/Common/SPIAdapterESP.h`), built on the ESP-IDF SPI master driver. `HandleType` is `void`.

```cpp
ASPI(spi_host_device_t spiHost, int mosi, int miso, int sclk, int cs = -1, uint32 maxTransfer = 4096);
```

- `maxTransfer` is the largest transfer the bus accepts, default 4096 bytes. It is limited by the driver's maximum transaction length for the bus.
- Synchronous transfers larger than the limit are split into chunks automatically. If `cs >= 0` (hardware chip select), the bus is acquired for the whole transfer, so chip select stays asserted across the chunks.
- Two adapters may share one SPI host. The first adapter that initializes the host defines the bus limit; a later adapter keeps its own limit, capped by the bus limit.
- Asynchronous transfers (`WriteAsync`, `ReadAsync`, `WriteReadAsync` and the array variants):
  - Only one transfer is in flight at a time. If the previous one has not finished, the call returns `busy`.
  - Asynchronous transfers are not chunked. A size above the limit returns `outOfRange`.
- `onInterrupt` is called from the ESP-IDF post-transaction callback (interrupt context) when an asynchronous transfer completes. Synchronous transfers do not call it.
- A synchronous call waits for a pending asynchronous transfer first. If it does not finish within the timeout, the call returns `timeout`.
- `Direction::Tx` marks the device as transmit-only. The driver then does not limit the clock to what the read path can sample, which is required above 26.6 MHz on pins routed through the GPIO matrix (for example a 40 MHz display).
- If the device cannot be added (for example an unsupported clock), `SetParameters()` returns `error` and the bus stays allocated until reset; a later `SetParameters()` with valid parameters reuses it.
- `Mode::Slave` returns `ResultStatus::notSupported`.
- Transfers return `noInit` until `SetParameters()` has initialized the device.

### Interrupt Context and IRAM

The post-transaction callback is `SPIAdapterESP::PostTransactionCallback`, a static member without `IRAM_ATTR`. The ESP-IDF documentation says the callback "should be in IRAM for best performance" and may crash during flash operation "when the driver is initialized with ESP_INTR_FLAG_IRAM" (`spi_master.h`, `post_cb`). The port does not set that flag, and it leaves `spi_bus_config_t::intr_flags` at 0, so the ISR is disabled during flash operations and the callback never runs with the flash cache off. The ESP-IDF guide says the same in its "Transferring Speed" section: "the ISR is disabled during flash operation by default. To keep sending transactions during flash operations, enable CONFIG_SPI_MASTER_ISR_IN_IRAM and set ESP_INTR_FLAG_IRAM in the member spi_bus_config_t::intr_flags" (`docs/en/api-reference/peripherals/spi_master.rst`).

`IRAM_ATTR` was removed because an in-class IRAM function fails to link at `-O2` with `dangerous relocation: l32r: literal placed after use`. If a future change sets `ESP_INTR_FLAG_IRAM`, the callback and everything it calls (including `CallInterrupt` and the `std::function`) must be placed in IRAM again.

### Performance Notes

- The bus is through the GPIO matrix on the M5StickC PLUS2 display pins (MOSI 15, SCLK 13, CS 5). The ESP32 IO_MUX pins for SPI2 are CS 15, SCLK 14, MISO 12 and MOSI 13, and `spi_master.rst` says the matrix limits signals to 40 MHz. The display runs at 40 MHz.
- A synchronous transfer of one area of up to the limit costs one driver call, so a pixel write above 4096 bytes is several transfers with chip select held. The display driver should send a whole area with one write (`ST7789::WritePixelBytes`), not per 256 pixels. In the display test the estimated SPI transaction count for a full frame went from 192 to 84 (an estimate from the code, not a bus probe).
- The ESP32 SPI DMA needs the transmit buffer in internal RAM (DRAM). A `static` array in `.bss` qualifies; a PSRAM buffer does not.
- `BeginWriteBytes` takes a buffer of any size up to `maxBulkTransactions` (8) transfers, which is 32 KB at the 4096-byte transfer limit. A 135 x 80 RGB565 band is 21600 bytes and takes six.
- The `WriteByteArrayAsync` path (`WriteArrayAsync` and the other `*Async` calls) is one transfer per call, with no chunking, and asserts chip select for that transfer only. Use `BeginWriteBytes` for a write that must span several transfers.

### Background Write on ESP32

`BeginWriteBytes` queues the transfers on the ESP-IDF transaction queue. It is the only port path that holds chip select across transfers, and it works as follows:

- A single transfer is queued with no flags. It needs no bus acquisition.
- Several transfers are queued with `SPI_TRANS_CS_KEEP_ACTIVE` on all but the last, after `spi_device_acquire_bus()`. The bus is released by `WaitWriteBytes` once every transfer has been collected.
- Completion is polled with `spi_device_get_trans_result()`. The transfers carry no user pointer, so the device's post-transaction callback does nothing for them, and no code runs in interrupt context on behalf of this path.
- `WaitWriteBytes(timeoutMs)` gives each transfer that time. `timeoutMs = 0` polls.
- A synchronous transfer on the port waits for a background write first, and so does teardown in `Deinitialize`.

ESP-IDF documents the chip-select rule (`components/esp_driver_spi/include/driver/spi_master.h`, `spi_device_queue_trans`):

> ESP_ERR_INVALID_ARG if parameter is invalid. This can happen if SPI_TRANS_CS_KEEP_ACTIVE flag is specified while the bus was not acquired (`spi_device_acquire_bus()` should be called first)

and the driver source (`components/esp_driver_spi/src/gpspi/spi_master.c`) rejects the flag when `host->device_acquiring_lock != handle`. The SPI Master guide says the same for back-to-back transactions: "To perform back-to-back transactions with a Device, call the function `spi_device_acquire_bus` before sending transactions and `spi_device_release_bus` after the transactions have been sent" (`docs/en/api-reference/peripherals/spi_master.rst`).

### DMA-Capable Memory

The buffer that `BeginWriteBytes` reads must be in internal RAM. `CheckDmaBuffer` uses `esp_ptr_dma_capable()` on the first and last byte. The ESP-IDF memory guide says of the DMA-capable allocation (`docs/en/api-reference/system/mem_alloc.rst`): "Use the `MALLOC_CAP_DMA` flag to allocate memory which is suitable for use with hardware DMA engines (for example SPI and I2S). This capability flag excludes any external PSRAM."

On the classic ESP32, PSRAM cannot be read by the SPI DMA. The PSRAM DMA feature (`SPI_TRANS_DMA_USE_PSRAM`) is documented only for chips with `SOC_PSRAM_DMA_CAPABLE`, which the ESP32 does not have. A `static` array is placed in internal DRAM unless it is given a PSRAM attribute, so the check passes for the display buffers. The check exists so a misplaced buffer returns `invalidParameter` at initialisation instead of corrupting the screen.

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
