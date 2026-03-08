# I2CAdapter

Abstract adapter for I2C (Inter-Integrated Circuit) peripherals. Supports master/slave modes, 7/10-bit addressing, configurable speed up to Fast Mode Plus, and both synchronous and asynchronous register-addressed communication.

Header: `#include <Adapter/I2CAdapter.h>`

## Template Parameter

```cpp
template<typename HandleType>
class I2CAdapter : public IAdapter;
```

`HandleType` — platform-specific I2C register structure.

## Parameters

```cpp
struct Parameters {
    Speed speed = Speed::Standard;
    Mode mode = Mode::Master;
    AddressingMode addressingMode = AddressingMode::B7;
    uint8 slaveAddress = 0;
    DutyCycle dutyCycle = DutyCycle::D2;
    bool analogFilter = false;
    uint8 digitalFilter = 0;
    bool clockStretching = true;
    uint8 riseTimeNs = 100;
    uint8 fallTimeNs = 10;
};
```

| Field | Options | Default | Description |
|-------|---------|---------|-------------|
| `speed` | `Low(10kHz)`, `Standard(100kHz)`, `Fast(400kHz)`, `FastPlus(1MHz)` | `Standard` | Bus clock speed |
| `mode` | `Master`, `Slave` | `Master` | I2C role |
| `addressingMode` | `B7`, `B10` | `B7` | Address bit width |
| `slaveAddress` | `uint8` | `0` | Own address in slave mode |
| `dutyCycle` | `D2`, `D16_9` | `D2` | Clock duty cycle (Fast mode) |
| `analogFilter` | `bool` | `false` | Enable analog noise filter |
| `digitalFilter` | `0..15` | `0` | Digital filter width |
| `clockStretching` | `bool` | `true` | Allow slave clock stretching |
| `riseTimeNs` | `uint8` | `100` | SDA/SCL rise time (ns) |
| `fallTimeNs` | `uint8` | `10` | SDA/SCL fall time (ns) |

## Enums

| Enum | Values | Description |
|------|--------|-------------|
| `Error` | `None`, `SlaveAddressMatch`, `Busy`, `ArbitrationLost`, `OverUnderRun`, `AcknowledgeFailure`, `MisplacedStartStop` | Error types |

## API

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(Parameters val)` | `ResultStatus` | Apply configuration |
| `SetTimeout(uint32 time)` | `void` | Set timeout for sync operations (ms) |
| `Await()` | `ResultStatus` | Block until async operation completes |
| `SetSlaveLiisten(bool mode)` | `ResultStatus` | Enable/disable slave listen mode |
| `CheckDevice(uint8 addr, uint16 repeat)` | `ResultStatus` | Ping a device address |
| `CheckDeviceAsync(uint8 addr, uint16 repeat)` | `ResultStatus` | Async device check |
| `Scan(uint8* list, uint8 size)` | `Result<uint8>` | Scan bus, returns number of found devices |
| `ScanAsync(uint8* list, uint8 size)` | `Result<uint8>` | Async bus scan |
| `WriteByteArray(uint8 device, uint16 addr, uint8 addrSize, uint8* data, uint32 size)` | `ResultStatus` | Write to device register |
| `ReadByteArray(uint8 device, uint16 addr, uint8 addrSize, uint8* data, uint32 size)` | `ResultStatus` | Read from device register |
| `WriteByteArrayAsync(...)` | `ResultStatus` | Async write |
| `ReadByteArrayAsync(...)` | `ResultStatus` | Async read |
| `IrqEventHandler()` | `void` | Call from I2C event IRQ |
| `IrqErrorHandler()` | `void` | Call from I2C error IRQ |

## Callbacks

| Callback | Type | Description |
|----------|------|-------------|
| `onSlaveRead` | `std::function<void(uint8 data)>` | Master wrote a byte (slave mode) |
| `onSlaveWrite` | `std::function<uint8()>` | Master requests a byte (slave mode) |
| `onSlaveEndTransfer` | `std::function<void()>` | Transfer ended (slave mode) |
| `onError` | `std::function<void(Error)>` | Communication error |
| `onComplete` | `std::function<void()>` | Async operation completed |

## BSP Configuration Example

```cpp
// BSP.h
class BSP {
public:
    static AI2C i2c1;
};

// BSP.cpp
AI2C BSP::i2c1 = { I2C1, SystemCoreClock };

void BSP::InitAdapterPeripheryEvents() {
    i2c1.beforePeripheryInit = []() {
        LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_I2C1);

        AGPIO::AlternateOpenDrainInit({ GPIOB, 6, 6, AGPIO::Pull::Up, AGPIO::Speed::VeryHigh }); // SCL
        AGPIO::AlternateOpenDrainInit({ GPIOB, 7, 6, AGPIO::Pull::Up, AGPIO::Speed::VeryHigh }); // SDA

        NVIC_SetPriority(I2C1_IRQn, 1);
        NVIC_EnableIRQ(I2C1_IRQn);

        return ResultStatus::ok;
    };
}

// DeviceIrq.cpp — I2C uses two IRQ lines
void I2C1_IRQHandler() {
    BSP::i2c1.IrqEventHandler();
    BSP::i2c1.IrqErrorHandler();
}
```

## Usage Example

```cpp
BSP::i2c1.SetParameters({
    .speed = AI2C::Speed::Fast,
    .mode = AI2C::Mode::Master
});

// Read WHO_AM_I register from sensor at address 0x68
uint8 whoAmI;
BSP::i2c1.ReadByteArray(0x68, 0x75, 1, &whoAmI, 1);

// Write configuration register
uint8 config = 0x01;
BSP::i2c1.WriteByteArray(0x68, 0x6B, 1, &config, 1);
```
