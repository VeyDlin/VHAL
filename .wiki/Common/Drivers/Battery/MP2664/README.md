# MP2664

MPS MP2664 single-cell Li-ion charger with power-path management, on I2C at address 0x09.

`VHAL/Common/Drivers/Battery/MP2664/MP2664.h`. It takes an `AI2C&` and does not configure the bus. It is header-only and
uses VHAL's `Result` and `ResultStatus`.

## API

```cpp
MP2664 charger(bus);                              // bus is an AI2C the caller has already configured
Result<MP2664::Status> status = charger.ReadStatus();
charger.SetChargingEnabled(true);                 // clears or sets the charge-disable bit
charger.SetInputCurrentLimitMa(455);              // one of the 8 supported values
charger.SetChargeCurrentMa(263);                  // 8 + 17 x code, 8 to 535 mA
```

`ReadStatus` reads registers 0x07 (system status) and 0x08 (fault) and returns a `Status` with:

| Field | Source | Meaning |
|---|---|---|
| `revision` | 0x07 bits 6:5 | Chip revision |
| `charging` | 0x07 bits 4:3 | `NotCharging`, `PreCharge`, `Charging`, `Done` |
| `powerPathManagement` | 0x07 bit 2 | Power-path (system supply) status |
| `powerGood` | 0x07 bit 1 | Input power is good |
| `thermalRegulation` | 0x07 bit 0 | Charger is reducing the current for temperature |
| `watchdogFault`, `inputFault`, `thermalShutdown`, `batteryFault`, `safetyTimerFault`, `ntcHot`, `ntcCold` | 0x08 bits 6, 5, 4, 3, 2, 1, 0 | Fault flags |

## Configuration

| Setter | Register | Accepted values |
|---|---|---|
| `SetInputCurrentLimitMa` | 0x00, bits 2:0 | 85, 130, 175, 220, 265, 310, 355, 455 mA. Any other value returns `outOfRange` and writes nothing |
| `SetChargeCurrentMa` | 0x02, bits 4:0 | 8 mA to 535 mA in 17 mA steps (`8 + 17 x code`, code 0 to 31). Other values return `outOfRange` |
| `SetChargingEnabled` | 0x01, bit 3 | `true` clears the bit (charging allowed), `false` sets it |

Each setter reads the register, changes only its own bits and writes it back. `SetChargingEnabled` masks the register with
0x3F before it writes, so it writes 0 to bits 7:6 of register 0x01. That is intended: bit 7 (register reset) and bit 6 (I2C
watchdog timer reset) are commands, where 1 resets and 0 keeps the current state. Copying them back from a read could repeat
a reset, so they are always written as 0. Bits 5:4 are reserved and kept as read.

## Notes

- Every call is one or more blocking I2C transactions. A failed transaction is returned as the `ResultStatus` of that call.
- The driver has no interrupt or polling loop. Call `ReadStatus` on the schedule the application needs.
- The registers and bits the driver uses are checked against the MPS datasheet "MP2664 – 0.5A, 1-cell charger w/ I2C
  control and power path", Rev. 1.01 (11/12/2018): I2C address 0x09; register 0x00 IIN_LIM table; 0x01 bit layout; 0x02
  ICC (8 mA offset, 17 mA step, 8 to 535 mA); 0x07 REV, CHG_STAT, PPM_STAT, PG_STAT, THERM_STAT; 0x08 fault bits. Registers
  the driver does not use (0x03 to 0x06: pre-charge and termination current, charge voltage, timers, miscellaneous) have no
  API yet.
