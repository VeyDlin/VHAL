# MPU6886

InvenSense/TDK MPU-6886 accelerometer, gyroscope and temperature sensor on I2C. On the M5StickC PLUS2 it sits on I2C0 (SDA 21,
SCL 22) at address 0x68, and `WHO_AM_I` (0x75) reads 0x19.

`VHAL/Common/Drivers/Sensor/MPU6886/MPU6886.h`. It takes an `AI2C&` and an address. It is header-only and uses VHAL's
`Result` and `ResultStatus`.

## Board facts

- The INT pin is not connected to the ESP32 on this board (measured with `tools/imu-wake-probe`). Every read is polled,
  and no interrupt is configured. The FIFO is drained on a timer instead.
- The caller starts the bus before `Init`: `imuBus.SetParameters({ .speed = AI2C::Speed::Fast })`. The driver does not
  reconfigure the bus.
- GPIO 23 must never be configured (it hangs the chip). The driver does not touch it.

## API

```cpp
MPU6886 imu(imuBus);                 // address 0x68
imu.Init();                          // default Config, or Init(config)
imu.CalibrateGyroBias(100, 10, 2.0f, bias);   // still device: busy if the spread exceeds 2 deg/s
imu.StartFifo();                     // accelerometer, temperature and gyroscope, 14 bytes per frame
imu.ReadFifo(frames, count, overflowed);      // at most fifoMaxFrames (16) per call
Result<MPU6886::Sample> s = imu.Read();       // one burst read of the 14 data bytes
```

`Sample` holds accelerometer `accelX/Y/Z` in g, gyroscope `gyroX/Y/Z` in deg/s and `temperatureC`.

`Init` does, in order: `PWR_MGMT_1` = 0x80 (reset), a 100 ms delay, read `WHO_AM_I` (mismatch gives `notRecognized`),
`PWR_MGMT_1` = 0x01 (wake, clock from the PLL), a 100 ms delay, `PWR_MGMT_2` = 0x00, then `CONFIG`, `SMPLRT_DIV`,
`GYRO_CONFIG`, `ACCEL_CONFIG`, `ACCEL_CONFIG2`. Each configuration write is read back and compared (`writeError` on a
mismatch). The scales change only when the whole sequence succeeds. `PWR_MGMT_2` must be written to 0x00: the probe left
the gyroscope in standby (0x07), which is why the default write matters.

Default `Config`:

| Field | Default | Meaning |
|---|---|---|
| `accelRange` | G4 | +-4 g, 8192 LSB/g |
| `gyroRange` | Dps500 | +-500 deg/s, 65.5 LSB/(deg/s) |
| `gyroDlpf` | 3 | `CONFIG.DLPF_CFG`: 41 Hz gyroscope bandwidth at 1 kHz |
| `accelDlpf` | 2 | `ACCEL_CONFIG2.A_DLPF_CFG`: 99 Hz accelerometer bandwidth at 1 kHz |
| `sampleRateDivider` | 0 | output rate 1 kHz / (1 + 0) = 1 kHz (the FIFO rate for taps) |

The gyroscope range is 500 deg/s, which covers the tilt and taps. A violent shake can exceed it (a documented limit).

## Registers used

Verified against the datasheet (DS-000193 Rev 1.1, M5Stack copy; see Sources):

| Register | Address | Use |
|---|---|---|
| SMPLRT_DIV | 0x19 | output rate = 1 kHz / (1 + div) with the DLPF on |
| CONFIG | 0x1A | DLPF_CFG bits 2:0; bit 6 FIFO_MODE = 0 (overwrite the oldest when full) |
| GYRO_CONFIG | 0x1B | FS_SEL bits 4:3 (0 250, 1 500, 2 1000, 3 2000 deg/s); FCHOICE_B 00 = DLPF used |
| ACCEL_CONFIG | 0x1C | AFS_SEL bits 4:3 (0 2 g, 1 4 g, 2 8 g, 3 16 g) |
| ACCEL_CONFIG2 | 0x1D | ACCEL_FCHOICE_B bit 3 = 0; A_DLPF_CFG bits 2:0 |
| INT_STATUS | 0x3A | bit 4 FIFO_OFLOW_INT, read-to-clear |
| ACCEL_XOUT_H..GYRO_ZOUT_L | 0x3B..0x48 | 14-byte burst: accel X, Y, Z, temperature, gyro X, Y, Z; each 16-bit, high byte first |
| FIFO_EN | 0x23 | bit 4 GYRO_FIFO_EN, bit 3 ACCEL_FIFO_EN (0x18 = both: 14-byte frames, same layout as the burst) |
| USER_CTRL | 0x6A | bit 6 FIFO_EN, bit 2 FIFO_RST (self-clearing) |
| PWR_MGMT_1 | 0x6B | bit 7 DEVICE_RESET (self-clearing), bits 2:0 CLKSEL (001 = PLL) |
| PWR_MGMT_2 | 0x6C | bits 5:0: standby per axis (0 = on) |
| FIFO_COUNTH/L | 0x72, 0x73 | byte count; reading the high byte latches both |
| FIFO_R_W | 0x74 | FIFO data, read burst |
| WHO_AM_I | 0x75 | 0x19 |

Scaling (datasheet sensitivity tables):

- accelerometer: 16384, 8192, 4096, 2048 LSB/g for 2, 4, 8, 16 g
- gyroscope: 131, 65.5, 32.8, 16.4 LSB/(deg/s) for 250, 500, 1000, 2000 deg/s
- temperature: `TEMP_degC = raw / 326.8 + 25`

Data rates with the DLPF on (`CONFIG.DLPF_CFG` 1 to 6 at a 1 kHz internal rate): gyroscope bandwidth 176, 92, 41, 20, 10,
5 Hz for 1 to 6. Accelerometer (A_DLPF_CFG 2 to 6): 99.0, 44.8, 21.2, 10.2, 5.1 Hz.

## FIFO

The FIFO is 1 kB. A frame is 14 bytes (accelerometer, temperature, gyroscope), so it holds about 73 frames, which is about
73 ms at 1 kHz. The test firmware drains it every 10 ms; `ReadFifo` returns at most 16 frames per call, and the caller
repeats the call while the FIFO returned a full 16.

`ReadFifo` does, in order:

1. Read INT_STATUS. If FIFO_OFLOW_INT is set (the flag clears on that read), the oldest data was lost: reset the FIFO
   (USER_CTRL = 0x44) and return `overflowed` with no frames.
2. Read FIFO_COUNTH and FIFO_COUNTL. A count above 1024 bytes is also treated as an overflow and reset.
3. Burst-read FIFO_R_W for the complete frames (`count / 14`, at most 16) and decode each frame with the same scaling as the
   burst read.

The frames have no timestamps. The caller must assign them with the true 1 ms spacing: a clock that advances by one frame
interval per frame, re-anchored to the timer only when it falls far behind (test firmware: `frameTimeUs`, 50 ms lag limit).
Timestamps taken from the read time instead are wrong when two drains read one or two frames: the spacing between frames
then shrinks, and any derivative (the tap jerk) is too large.

## Gyroscope bias

`CalibrateGyroBias(count, intervalMs, maxSpreadDps, bias)` averages `count` readings taken `intervalMs` apart. It returns
`busy` and no bias when any axis moved more than `maxSpreadDps` between the minimum and the maximum. The test firmware
measured x 1.026, y 0.874, z 0.134 deg/s on the board (lying still).

## Tests

`tools/vhal-host-tests/motion/run.sh` (`[HOST] motion.mpu6886.*`). The driver compiles against `stubs/VHAL.h`, a simulated
device with a register file, a write log, the FIFO, read-to-clear INT_STATUS and FIFO_RST. The tests check the write
sequence of `Init`, a dropped configuration write (readback mismatch), wrong `WHO_AM_I`, the burst decode and scaling,
gyroscope bias calibration (and that it rejects motion), the FIFO drain and the overflow reset. The compile check is
`tools/vhal-harness/main/checks/CheckMotion.cpp`.

## Sources

- Datasheet: MPU-6886 DS-000193 Rev 1.1 (M5Stack-hosted copy; the TDK page lists no register map). Known typos in that copy:
  the reset-value note names register 107 for PWR_MGMT_1 (it is 0x6B) and section 9.9 places SLEEP in PWR_MGMT_2 (it is
  in PWR_MGMT_1).
- Studied implementations: M5Unified `src/utility/imu/MPU6886_Class.inl` (14-byte burst from 0x3B, the init writes), ESPHome
  `components/mpu6886/mpu6886.cpp` (the same register map and scales).

## Not verified

- The post-reset delay after DEVICE_RESET. The datasheet gives none; 100 ms is a choice here.
- The reset-to-wake timing used by M5Unified (only the writes are visible in its code).
- The 100 ms wake delay: the datasheet gives gyroscope ready 35 ms typical, 100 ms maximum.
