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

Replace `gpioPort`, `sclNumber`, and `sdaNumber` with the port and pin values for the target.

## Requirements

- **Pull-up resistors are required on SDA and SCL.** The lines are open-drain and are never driven high, so without pull-ups they stay low.
- **Single master only.** Nothing arbitrates between masters. A second master shows up only as a held line.

## Pins

`SetPins` sets both lines to open-drain, releases them, and checks the bus. It returns the error from a failed pin setup. A held SCL is busy at once. A held SDA gets nine recovery clocks and a STOP, and is busy only if it stays low. Transfers before a successful `SetPins` return `ResultStatus::noInit` and touch no pin. The destructor releases the lines only when `SetPins` has attached them.

## Speed

`SetFrequency(kHz)` accepts 1 to 400 kHz and returns `ResultStatus::invalidParameter` for any other value. The default is 100 kHz. A bit lasts whole microseconds, rounded up, so the bus never runs faster than requested: a request runs at the highest achievable rate that is not above it. The achievable rates from 100 kHz up are:

| Bit period | Rate |
| --- | --- |
| 3 us | 333.33 kHz |
| 4 us | 250 kHz |
| 5 us | 200 kHz |
| 6 us | 166.67 kHz |
| 7 us | 142.86 kHz |
| 8 us | 125 kHz |
| 9 us | 111.11 kHz |
| 10 us | 100 kHz |

So `SetFrequency(100)` runs at exactly 100 kHz, and `SetFrequency(400)` runs at 333 kHz.

The two halves of each bit add up to the bit period. At 100 kHz and below the bit is split in half; an odd period gives the extra microsecond to one half. Above 100 kHz the low half is two thirds of the bit, rounded up, and the high half is the rest, so a 3 us bit has 2 us low and 1 us high. These halves meet the I2C minimums: Standard mode at 100 kHz (4.7 us low, 4.0 us high, 4.7 us bus free after a STOP), and Fast mode above it (1.3 us low, 0.6 us high, 1.3 us bus free). The default `dutyCycle` `D2` is accepted, but the ratio is exactly 2:1 only for 3, 6 and 9 us bits; other periods round the low half up.

400 kHz is the highest speed. Fast-mode Plus (1 MHz) is refused, because a bit of one microsecond has no low half that the minimum allows.

Each pin access on the target takes extra time. It only lengthens the halves of a bit, so the real rate is a little lower than the one in the table. Check the rate on the board.

Interrupts are not masked during a bit. I2C sets no maximum clock period outside SMBus, and the master owns SCL, so an interrupt only lengthens a bit. The target sees a longer low or high phase, which is legal.

## Transfers

`WriteByteArray` and `ReadByteArray` take a device address from 0x00 to 0x7F and a register address of 0, 1, or 2 bytes:

- A 2-byte register address goes out high byte first, the same as the hardware adapters.
- A register address size of 0 sends no register address.
- A read with a register address sends a repeated START between the address and the read.
- Each transfer sends a STOP at the end. A failed START sends no STOP.

`Read<T, A>` and `Write<T, A>` from the base class are available on `SoftwareI2C` directly. `Read` returns one value. Use `ReadArray` for more than one.

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

## Manual sessions

`Open`, `RequestWrite`, `RequestRead`, `Restart`, `Write`, `Read`, `ReadLast`, and `Close` build a transfer by hand. `Open` sends the START and `Close` sends the STOP. `Restart` sends a repeated START. The total transfer timeout starts at `Open`, `Restart`, `RequestWrite`, or `RequestRead`, and covers the bytes that follow it.

`RequestWrite`, `RequestRead`, and `Write` return `nack` without calling `onError`. Only a transfer calls `onError`. A probe from `CheckDevice` or `Scan` does not call it either.

## Results

| Result | Meaning |
| --- | --- |
| `ok` | The transfer completed. |
| `nack` | The target did not acknowledge the address or a data byte. In a transfer, a STOP is sent and `onError` is called with `Error::AcknowledgeFailure`. A manual call returns `nack` without calling `onError`. |
| `busy` | A line stays held. Before any START, a held SCL or a held SDA that recovery does not free is busy, and no START is sent. During a transfer, a transmitted 1 that reads low, a held SDA at a repeated START, or a STOP that cannot complete is busy. |
| `timeout` | A clock stretch lasted longer than the per-wait timeout, or the transfer lasted longer than the total timeout. |
| `invalidParameter` | A device address above 0x7F, a register address size above 2, an address that does not fit its size, a zero or above-400 kHz frequency, or a zero transfer timeout. Nothing is sent. |
| `notSupported` | A parameter value this driver cannot apply (see Parameters), or an asynchronous method. |
| `noInit` | A transfer or primitive was called before a successful `SetPins`. |

`CheckDevice(device, repeat)` sends a START and the address, then a STOP, and repeats the probe up to `repeat` times. It returns `ok` on the first acknowledge, and `nack` when every probe is refused. A held line, a timeout, or a device address above 0x7F returns that status at once, and `repeat` of 0 returns `invalidParameter`. Before `SetPins` it returns `noInit`.

`Scan(list, size)` probes addresses 0x08 to 0x77 and returns the number of devices found, up to `size`. The addresses 0x00 to 0x07 and 0x78 to 0x7F are reserved by the I2C specification, and they are not probed. A `busy` or `timeout` stops the scan and returns that status. The devices found before it are not returned.

## Recovery

If SDA is held low when a transfer or `SetPins` starts, the driver sends nine clocks with SDA released, then a STOP. A slave that releases SDA within those clocks is recovered. If SCL is held low, nothing is clocked and the result is `busy`.

## Timeouts

- `SetTimeout(milliseconds)` limits each wait for SCL to rise, which is clock stretching. The default is 1000 ms.
- `SetTransferTimeout(milliseconds)` limits a whole transfer, including recovery and every stretch. The timer starts at the beginning of each transfer, or at the manual calls listed above. The default is 1000 ms. The value must be above zero.

## Parameters

`SetParameters` applies `speed` and accepts these values: `speed` `Low` (10 kHz), `Standard` (100 kHz), or `Fast` (400 kHz); `mode` `Master`; `addressingMode` `B7`; and `clockStretching` `true`. Every other field must keep its default. Any other value returns `notSupported` and leaves the current settings unchanged. `FastPlus` is refused, and it is not run at `Fast` instead.

A bit-banged master cannot apply these fields, so a value other than the default is refused rather than ignored:

- `analogFilter` and `digitalFilter`: the driver has no filter on its pins.
- `dutyCycle`: the driver makes its own split (see Speed). `D16_9` is refused.
- `slaveAddress`: a master has no address of its own.
- `riseTimeNs` and `fallTimeNs`: the pull-ups and the bus capacitance set the edges, not the master.

## Not supported

Slave mode, 10-bit addressing, Fast-mode Plus, asynchronous transfers, and slave callbacks are not supported. The asynchronous methods and slave listening return `ResultStatus::notSupported`.
