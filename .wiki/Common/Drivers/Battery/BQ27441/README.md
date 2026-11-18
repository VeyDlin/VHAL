# BQ27441

TI BQ27441-G1 fuel gauge (battery monitor) on I2C at address 0x55. It reports voltage, average current, state of charge,
temperature and status flags, and it can write the battery pack parameters into the chip's Data Memory.

`VHAL/Common/Drivers/Battery/BQ27441/BQ27441.h`. It takes an `AI2C&` and an optional delay function. It is header-only and
uses VHAL's `Result` and `ResultStatus`.

## API

```cpp
BQ27441 gauge(bus, delayMs);                      // delayMs: void(uint32) or nullptr

Result<bool> needs = gauge.NeedsConfiguration();  // ITPOR set, or BAT_DET clear

BQ27441::Profile profile;
profile.designCapacityMah = 1000;
profile.terminateVoltageMv = 3000;
profile.taperCurrentMa = 100;
if (profile.IsValid()) {
    gauge.Configure(profile);
}

Result<BQ27441::Measurements> m = gauge.ReadMeasurements();
```

`ReadMeasurements` returns:

| Field | Command | Unit |
|---|---|---|
| `temperatureDeciKelvin` | 0x02 | 0.1 K |
| `voltageMv` | 0x04 | mV |
| `rawFlags` and `flags` | 0x06 | bit flags (see below) |
| `averageCurrentMa` | 0x10 | mA, signed (negative while discharging) |
| `stateOfChargePercent` | 0x1C | % |

The flags are decoded as `overTemperature`, `underTemperature`, `fullCharge`, `chargingAllowed`, `initialPowerOnReset`,
`configurationUpdate`, `batteryDetected`, `stateOfChargeLow`, `stateOfChargeCritical` and `discharging`. Each is
read from the bit of the same name in the Flags register.

## Profile

`Profile` holds the three values the driver writes into Data Memory:

| Field | Range | Written to |
|---|---|---|
| `designCapacityMah` | 1 to 8856 | State subclass, Design Capacity |
| `terminateVoltageMv` | 2800 to 3700 | State subclass, Terminate Voltage |
| `taperCurrentMa` | must be non-zero | Used for Taper Rate |

Two values are derived: Design Energy is `designCapacityMah x 3.7 V`, and Taper Rate is `designCapacityMah x 10 / taperCurrentMa`.
`IsValid()` also requires the Taper Rate to be between 1 and 2000. `Configure` returns `invalidParameter` for an invalid profile
without touching the chip.

## Configure sequence

`Configure` follows the TI TRM (SLUUAC9, section 3.1):

1. UNSEAL, sent twice, then SET_CFGUPDATE. The driver waits until the CONFIG UPDATE flag is set.
2. Writes the State subclass (class 82) and the Registers subclass (class 64). Each 32-byte block is written to Data
   Memory only when its checksum is written to register 0x60. The Registers subclass clears the BIE bit of OpConfig, so
   BAT_INSERT is the battery presence source.
3. SOFT_RESET, and waits until CONFIG UPDATE is clear.
4. SEAL, then BAT_INSERT.

Every wait is bounded by `FlagPollLimit` (120 polls at `FlagPollIntervalMs`, 10 ms). The poll delay is used only when a
delay function is given; without one the polls run back to back. If a step fails after CONFIG UPDATE was entered, the driver
still tries to leave it and to re-seal, and it returns the first error.

## Notes

- `NeedsConfiguration()` tells the application whether `Configure` has to run. It is true after a power-on reset (ITPOR) and
  when no battery is detected (BAT_DET clear). Call it once at start-up, not on every poll.
- Configuration writes to Data Memory are only valid during CONFIG UPDATE. The driver handles that; do not call the
  registers from the application.
- Register words (`ReadWord`, Control subcommands) are little-endian. Values inside the 32-byte Data Memory blocks are
  big-endian. The block checksum is `255 - (sum of the 32 block bytes mod 256)`.
