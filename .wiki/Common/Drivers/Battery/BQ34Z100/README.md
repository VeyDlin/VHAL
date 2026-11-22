# BQ34Z100

Driver for the TI BQ34Z100 battery fuel gauge, in `Common/Drivers/Battery/BQ34Z100/BQ34Z100.h`, with the register map in
`BQ34Z100Registers.h`. It is an I2C driver: it reads the gauge's measurements (voltage, current, temperature, capacity, time
to empty or full) and writes its flash subclasses, and it converts the sense resistor and the gauge's own float format.

## Number types

Float by design. The measurements are returned as `float` (`VoltageMeasurement::v()`, `CurrentMeasurement::a()`,
`TemperatureMeasurement::c()` and `k()`, `CapacityMeasurement::ah()`, `TimeMeasurement::hours()`), and the gauge's Xemics float
format is converted with `logf`, `powf` and `powf` with a runtime exponent. A fixed-point type cannot compute those conversions,
so the class is not a template and `IQ<Q>` is not supported. The battery level is computed from these float values by
`BatteryLevel` (`Utilities/Hardware/BatteryLevel`), which takes a template type.
