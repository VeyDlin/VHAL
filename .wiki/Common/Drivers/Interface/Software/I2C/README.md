# SoftwareI2C

`SoftwareI2C` is a synchronous, master-mode I2C implementation that drives SDA and SCL through GPIO pins. Include `Drivers/Interface/Software/I2C/SoftwareI2C.h`, provide the two `AGPIO::IO` pin descriptions, and set the bus speed in kHz:

```cpp
SoftwareI2C bus;
AGPIO::IO sclPin{ gpioPort, sclNumber };
AGPIO::IO sdaPin{ gpioPort, sdaNumber };

ResultStatus pinStatus = bus.SetPins(sclPin, sdaPin);
if (pinStatus == ResultStatus::ok) {
    bus.SetFrequency(100);
}
```

Replace `gpioPort`, `sclNumber`, and `sdaNumber` with the port and pin values for the target. `SetPins` configures both lines as open-drain and releases the bus. The destructor also releases the lines.

Use the inherited I2C register helpers for typed operations. `Read` returns `Result<T>`; check the status before accessing its value. `ReadByteArray` and `WriteByteArray` return `ResultStatus` directly:

```cpp
I2CAdapter<void>& adapter = bus;
Result<uint8> readResult = adapter.Read<uint8, uint8>(0x48, 0x00);
if (readResult.IsOk()) {
    uint8 deviceId = readResult.Value();
}

uint8 configuration = 0x01;
ResultStatus writeStatus = bus.WriteByteArray(
    0x48,
    0x01,
    1,
    &configuration,
    sizeof(configuration)
);
```

Clock stretching is enabled internally. The implementation waits for SCL to reach high and returns `ResultStatus::timeout` when it remains low until the adapter timeout; set that timeout with `SetTimeout(milliseconds)`. `SetFrequency(kHz)` expects a positive value and caps requests at 1000 kHz.

This implementation does not support device scanning, device checks, slave mode, or asynchronous transfers. Those adapter methods return `ResultStatus::notSupported` (or a `Result<uint8>` containing that error). The inherited slave callbacks are therefore not used by `SoftwareI2C`.
