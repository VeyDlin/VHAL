# Modbus RTU

Modbus RTU Master/Slave implementation for embedded systems. Callback-based architecture -- no dependency on a specific UART driver, works with any transport.

## Quick Start -- Master

```cpp
#include <Drivers/Interface/Modbus/RTU/ModbusMaster.h>

ModbusMaster master(1);  // target slave ID

master.SetSendCallback([](std::span<const uint8> data) {
    uart.SendData(data.data(), data.size());
});

master.SetReceiveCallback([](std::span<uint8> buffer) -> size_t {
    return uart.ReceiveData(buffer.data(), buffer.size());
});

// Read 10 holding registers starting at address 200
std::array<uint16, 10> registers{};
auto err = master.ReadHoldingRegisters(200, std::span(registers));
if (err == ModbusMaster::ModbusError::Success) {
    // registers[] now contains data from slave
}
```

## Quick Start -- Slave

```cpp
#include <Drivers/Interface/Modbus/RTU/ModbusSlave.h>

ModbusSlave slave(1);  // this device's address

slave.SetSendCallback([](std::span<const uint8> data) {
    uart.SendData(data.data(), data.size());
});

// Map data arrays
std::array<uint16, 200> holdingRegs{};
std::array<uint16, 100> inputRegs{};
slave.SetHoldingRegisterData(std::span(holdingRegs));
slave.SetInputRegisterData(std::span(inputRegs));

// Process incoming requests (call from UART RX handler)
void OnDataReceived(std::span<const uint8> data) {
    slave.ProcessRequest(data);
}
```

## Supported Functions

| Code | Function | Master | Slave |
|------|----------|--------|-------|
| 0x01 | Read Coils | `ReadCoils()` | auto |
| 0x02 | Read Discrete Inputs | `ReadDiscreteInputs()` | auto |
| 0x03 | Read Holding Registers | `ReadHoldingRegisters()` | auto |
| 0x04 | Read Input Registers | `ReadInputRegisters()` | auto |
| 0x05 | Write Single Coil | `WriteSingleCoil()` | auto |
| 0x06 | Write Single Register | `WriteSingleRegister()` | auto |
| 0x0F | Write Multiple Coils | `WriteMultipleCoils()` | auto |
| 0x10 | Write Multiple Registers | `WriteMultipleRegisters()` | auto |

## Error Handling

Master methods return `ModbusError`:

```cpp
auto result = master.ReadHoldingRegisters(100, std::span(data));

switch (result) {
    case ModbusError::Success:           break;  // OK
    case ModbusError::Timeout:           break;  // no response within timeout
    case ModbusError::CrcError:          break;  // CRC mismatch
    case ModbusError::ExceptionResponse: break;  // slave returned exception
    case ModbusError::InvalidResponse:   break;  // malformed response
    case ModbusError::BufferTooSmall:    break;  // request exceeds protocol limits
}
```

Slave sends standard Modbus exception responses automatically:

| Exception | Meaning |
|-----------|---------|
| `IllegalFunction` | Unknown function code |
| `IllegalDataAddress` | Address out of range or rejected by validator |
| `IllegalDataValue` | Invalid quantity or byte count |
| `SlaveDeviceFailure` | Write callback returned `false` |

## Data Model

The slave maps four data areas via `std::span`:

| Area | Type | Access | Method |
|------|------|--------|--------|
| Coils | `bool` | Read/Write | `SetCoilData()` |
| Discrete Inputs | `bool` | Read only | `SetDiscreteInputData()` |
| Holding Registers | `uint16` | Read/Write | `SetHoldingRegisterData()` |
| Input Registers | `uint16` | Read only | `SetInputRegisterData()` |

Direct access helpers: `SetCoil()`, `GetCoil()`, `SetRegister()`, `GetRegister()`.

## Write Callbacks

Intercept writes for validation or side effects:

```cpp
slave.SetCoilWriteCallback([](uint16 address, bool value) -> bool {
    if (address == 100) {
        SetOutputPin(value);
        return true;   // accept
    }
    return false;  // reject → SlaveDeviceFailure exception
});

slave.SetRegisterWriteCallback([](uint16 address, uint16 value) -> bool {
    if (address >= 200 && address < 210) {
        SetParameter(address - 200, value);
        return true;
    }
    return false;
});
```

Without callbacks, writes go directly to the mapped data arrays.

## Address Validation

```cpp
slave.SetAddressValidator([](uint16 address, uint16 quantity, DataType type) -> bool {
    if (type == DataType::HoldingRegisters) {
        return address >= 100 && (address + quantity) <= 500;
    }
    return true;  // allow all other types
});
```

Returning `false` sends `IllegalDataAddress` exception.

## Configuration

**Master:**

```cpp
master.SetTimeout(2000);        // response timeout in ms (default: 1000)
master.SetSlaveId(5);           // change target slave
master.SetTickCallback([]() {   // custom time source (default: System::GetTick())
    return HAL_GetTick();
});
```

**Slave:**

```cpp
slave.SetExceptionCallback([](ModbusException ex, uint16 addr) {
    Log("Exception %d at %d", (int)ex, addr);
});
```

## Protocol Limits

| Parameter | Limit |
|-----------|-------|
| Max frame size | 256 bytes |
| Coils per read/write | 2000 / 1968 |
| Registers per read | 125 |
| Registers per write | 123 |
