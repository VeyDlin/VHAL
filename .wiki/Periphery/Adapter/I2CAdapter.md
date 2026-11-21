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
| `CheckDevice(uint8 addr, uint16 repeat = 1)` | `ResultStatus` | Ping a device address |
| `CheckDeviceAsync(uint8 addr, uint16 repeat = 1)` | `ResultStatus` | Async device check |
| `Scan(uint8* list, uint8 size)` | `Result<uint8>` | Scan bus, returns number of found devices |
| `ScanAsync(uint8* list, uint8 size)` | `Result<uint8>` | Async bus scan |
| `Write<DataType, AddressType>(uint8 device, AddressType address, DataType data)` | `ResultStatus` | Write one typed value to a register |
| `WriteArray<DataType, AddressType>(uint8 device, AddressType address, DataType* buffer, uint32 size)` | `ResultStatus` | Write a typed array to a register |
| `Read<DataType, AddressType>(uint8 device, AddressType address)` | `Result<DataType>` | Read one typed value from a register. Use `ReadArray` for more than one |
| `ReadArray<DataType, AddressType>(uint8 device, AddressType address, DataType* buffer, uint32 size = 1)` | `ResultStatus` | Read a typed array from a register |
| `WriteAsync<DataType, AddressType>(uint8 device, AddressType address, DataType data)` | `ResultStatus` | Start an async typed write |
| `WriteArrayAsync<DataType, AddressType>(uint8 device, AddressType address, DataType* buffer, uint32 size)` | `ResultStatus` | Start an async typed array write |
| `ReadAsync<DataType, AddressType>(uint8 device, AddressType address)` | `Result<DataType>` | Start an async typed read of one value. Use `ReadArrayAsync` for more than one |
| `ReadArrayAsync<DataType, AddressType>(uint8 device, AddressType address, DataType* buffer, uint32 size = 1)` | `ResultStatus` | Start an async typed array read |
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
| `onSlaveAddressMatch` | `std::function<void(bool isRead)>` | Own address matched, before the first data byte (slave mode) |
| `onSlaveStop` | `std::function<void(bool wasRead)>` | STOP condition received (slave mode) |
| `onError` | `std::function<void(Error)>` | Communication error |
| `onComplete` | `std::function<void()>` | Async operation completed |

## Bus Recovery

A slave that is reset in the middle of a transfer can keep SDA low after the master has restarted, and every later transaction then times out. Ports should release a held bus at initialisation, before the peripheral takes over the pins: clock SCL with SDA released, then send a STOP. The ESP32 port does this in `SetParameters()` and returns `ResultStatus::error` when SDA stays low.

## ESP32 Port

`AI2C` is `I2CAdapterESP` (`Periphery/Adapter/Port/ESP32/Common/I2CAdapterESP.h`), built on the ESP-IDF I2C master driver. `HandleType` is `void`.

```cpp
AI2C(i2c_port_num_t port, int sda, int scl);
```

- Master only. `Mode::Slave` returns `ResultStatus::notSupported`.
- Operations return `noInit` until `SetParameters()` has created the bus.
- Read and write calls with a device address of 128 or more return `ResultStatus::badAddress`. The address is compared against 128 only in these calls; `CheckDevice` and `Scan` do not check it.
- `digitalFilter`: when non-zero it is used as the glitch filter width. When zero, the driver uses 7.
- Asynchronous methods (`CheckDeviceAsync`, `ScanAsync`, `WriteByteArrayAsync`, `ReadByteArrayAsync`) return `ResultStatus::notSupported`.
- Internal pull-ups: the bus is created with the internal pull-ups enabled when `analogFilter` is `false`, and with them disabled when `analogFilter` is `true`.
- Bus recovery: `SetParameters()` releases a held bus before it creates the driver bus. It sends nine SCL pulses with SDA released, then a STOP, on the same pins (bit-banged through GPIO). The nine pulses are sent even when SDA is already high, because a slave reset in the middle of a byte can release SDA between two bits and still expect the clocks. If SDA or SCL is still low after the pulses, `SetParameters()` returns `ResultStatus::error` and no bus is created.
- Recovery runs only when `SetParameters()` is called, so a sensor that hangs later in the session is not released until the next `SetParameters()`.

## STM32 G4 Port

`AI2C` is `I2CAdapterG4` (`Periphery/Adapter/Port/STM32/G4/I2CAdapterG4.h`), on the I2C v2 IP (the same IP as G0). `HandleType` is `I2C_TypeDef`. The port is slave only: the master operations (`Read`, `Write`, `*ByteArray`, `CheckDevice`, `Scan`) return `ResultStatus::notSupported`.

- Slave listen: `SetSlaveLiisten(true)` enables the own address (`Parameters::slaveAddress`, 7-bit), the address, NACK, error and STOP interrupts. Clock stretching follows `clockStretching` and is enabled by default.
- Callbacks run from the interrupt, in this order for one transaction:
  - `onSlaveAddressMatch(isRead)` on every address match, before the first data byte. The application resets its TX index here, because the first TXIS follows.
  - `onSlaveRead(data)` for every byte the master writes.
  - `onSlaveWrite()` for every byte the master reads, called once per byte.
  - `onSlaveStop(wasRead)` and `onSlaveEndTransfer()` on every STOP.
- A bus error, arbitration loss or overrun calls `onError` and aborts the current transfer. The listen state stays armed, so the next transaction is served normally. A STOP callback may not follow the aborted transfer.
- Timing is computed for a 16 MHz kernel clock (HSI16) with a zero prescaler. Other clock frequencies return `ResultStatus::notSupported` from `SetParameters()`. Only `Standard` (100 kHz) and `Fast` (400 kHz) are accepted.
- Interrupts: call `IrqHandler()` from one vector that serves both `I2C_EV` and `I2C_ER`. It dispatches to `IrqErrorHandler()` when BERR, ARLO or OVR is set, and to `IrqEventHandler()` otherwise.

```cpp
BSP::i2c1.SetParameters({
    .speed = AI2C::Speed::Fast,
    .mode = AI2C::Mode::Slave,
    .slaveAddress = 0x2A
});
BSP::i2c1.onSlaveAddressMatch = [](bool isRead) {
    // Start a new transfer: reset the register pointer or TX index here
};
BSP::i2c1.SetSlaveLiisten(true);

void I2C1_IRQHandler() {
    BSP::i2c1.IrqHandler();
}
```

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

// Read WHO_AM_I register from a sensor at address 0x68
Result<uint8> whoAmIResult = BSP::i2c1.Read<uint8>(0x68, static_cast<uint8>(0x75));
if (whoAmIResult.IsOk()) {
    uint8 whoAmI = whoAmIResult.Value();
}

// Write configuration register
uint8 config = 0x01;
BSP::i2c1.Write<uint8>(0x68, static_cast<uint8>(0x6B), config);
```
