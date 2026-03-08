# Termistor

NTC thermistor temperature measurement library with table-based lookup, interpolation, and extrapolation.

---

## Table of Contents

- [Quick Start](#quick-start)
- [ITermistor Base Class](#itermistor-base-class)
  - [Configuration](#configuration)
  - [Resolution](#resolution)
  - [Thermistor Position](#thermistor-position)
  - [GetTemperature](#gettemperature)
  - [TermistorConfig](#termistorconfig)
- [Supported Thermistors](#supported-thermistors)
- [Adding a Custom Thermistor](#adding-a-custom-thermistor)
- [API Reference](#api-reference)

---

## Quick Start

```cpp
#include <Drivers/Termistor/Type/B57541G1103F000.h>

Termistor_B57541G1103F000 thermistor;

// Configure for your circuit
ITermistor<>::Config cfg;
cfg.thermistorPosition = ITermistor<>::Position::Top;
cfg.dividerResistor    = 10;                      // kOhm
cfg.adcReference       = 3.3;                     // V
cfg.adcResolution      = ITermistor<>::Resolution::B12;

thermistor.SetConfig(cfg);

// Read temperature from a raw ADC value
uint16_t adcValue = ReadADC();
float temperature = thermistor.GetTemperature(adcValue); // degrees Celsius
```

---

## ITermistor Base Class

`template <RealType Type = float>`

`ITermistor` is the abstract base class that all thermistor types inherit from. It handles:

- Conversion of raw ADC values to resistance using a voltage divider model
- Lookup of temperature from resistance using a per-thermistor table
- Linear interpolation between table entries
- Linear extrapolation beyond table boundaries

### Configuration

Set the hardware parameters with `SetConfig()`:

```cpp
ITermistor<>::Config cfg;
cfg.thermistorPosition = ITermistor<>::Position::Bottom; // thermistor on the low side
cfg.dividerResistor    = 4.7;                          // 4.7 kOhm fixed resistor
cfg.adcReference       = 5.0;                          // 5V reference
cfg.adcResolution      = ITermistor<>::Resolution::B10;  // 10-bit ADC

thermistor.SetConfig(cfg);
```

| Field                | Type         | Default          | Description                          |
|----------------------|--------------|------------------|--------------------------------------|
| `thermistorPosition` | `Position`   | `Position::Top`  | Thermistor placement in the divider  |
| `dividerResistor`    | `Type`       | `10`             | Fixed resistor value in kOhm         |
| `adcReference`       | `Type`       | `3.3`            | ADC reference voltage in V           |
| `adcResolution`      | `Resolution` | `Resolution::B12`| ADC bit depth                        |

### Resolution

The `Resolution` enum defines the ADC maximum value for common bit depths:

| Value  | Max ADC Value |
|--------|---------------|
| `B4`   | 15            |
| `B5`   | 31            |
| `B6`   | 63            |
| `B7`   | 127           |
| `B8`   | 255           |
| `B9`   | 511           |
| `B10`  | 1023          |
| `B11`  | 2047          |
| `B12`  | 4095          |
| `B13`  | 8191          |
| `B14`  | 16383         |
| `B15`  | 32767         |
| `B16`  | 65535         |

### Thermistor Position

Defines where the thermistor sits in the resistor divider relative to the ADC input:

| Value    | Circuit                                      |
|----------|----------------------------------------------|
| `Top`    | VCC --- [Thermistor] --- ADC --- [Resistor] --- GND |
| `Bottom` | VCC --- [Resistor] --- ADC --- [Thermistor] --- GND |

### GetTemperature

```cpp
float temperature = thermistor.GetTemperature(adcValue);
```

Converts a raw ADC reading into temperature in degrees Celsius. Internally:

1. Calculates thermistor resistance from the ADC value using the voltage divider formula.
2. Looks up the resistance in the thermistor's R-T table.
3. Uses linear interpolation if the resistance falls between two table entries.
4. Uses linear extrapolation if the resistance is outside the table range.

### TermistorConfig

The protected struct that each thermistor subclass must populate:

```cpp
struct TermistorConfig {
    uint8 r25;                   // Nominal resistance at 25 C (kOhm)
    const Type (*table)[2];      // Pointer to {temperature, resistance} table
    uint8 tableSize;             // Number of entries in the table
};
```

The table must be sorted by **descending resistance** (lowest temperature first).

---

## Supported Thermistors

| Class                              | Part Number           | R25 (kOhm) | Range         | Table Points |
|------------------------------------|-----------------------|-------------|---------------|--------------|
| `Termistor_B57541G1103F000`        | Epcos B57541G1103F000 | 10          | -55 to 155 C  | 43           |
| `Termistor_B57861S0103`            | Epcos B57861S0103     | 10          | -55 to 155 C  | 43           |
| `Termistor_GT103J1K`              | GT103J1K              | 10          | -40 to 300 C  | 99           |
| `Termistor_NTCSP163JF103FT1S`     | TDK NTCSP163JF103FT1S| 10          | -40 to 150 C  | 191          |

Usage is identical for all types -- just include the corresponding header and instantiate:

```cpp
#include <Drivers/Termistor/Type/GT103J1K.h>

Termistor_GT103J1K thermistor;
thermistor.SetConfig(cfg);
float temp = thermistor.GetTemperature(adcValue);
```

---

## Adding a Custom Thermistor

Create a new header in `Type/` and inherit from `ITermistor`:

```cpp
#pragma once
#include <Drivers/Termistor/ITermistor.h>

template <RealType Type = float>
class Termistor_MyCustomNTC : public ITermistor<Type> {
protected:
    // Table: {temperature (C), resistance (kOhm)}, sorted by descending resistance
    const Type resistanceTable[5][2] = {
        {-20.0, 97.0},
        {  0.0, 28.7},
        { 25.0, 10.0},
        { 50.0,  4.1},
        {100.0,  0.9}
    };

    typename ITermistor<Type>::TermistorConfig termistorConfig;

    virtual typename ITermistor<Type>::TermistorConfig* GetTermistorConfig() override {
        return &termistorConfig;
    }

public:
    Termistor_MyCustomNTC() {
        termistorConfig.r25 = 10;
        termistorConfig.table = resistanceTable;
        termistorConfig.tableSize = 5;
    }
};
```

---

## API Reference

### `ITermistor` (abstract)

| Method / Type                  | Description                                                    |
|-------------------------------|----------------------------------------------------------------|
| `void SetConfig(Config val)`  | Sets the hardware configuration (divider, ADC, position).      |
| `Type GetTemperature(uint16 adcVal)` | Returns temperature in Celsius from a raw ADC value.  |
| `enum Resolution`             | ADC bit-depth constants (B4 through B16).                      |
| `enum Position`               | Thermistor placement: `Top` or `Bottom`.                       |
| `struct Config`               | Hardware configuration container.                              |

### Thermistor subclasses

All subclasses are `template <RealType Type = float>` and share the same public API inherited from `ITermistor<Type>`. They only differ in the built-in resistance-temperature table.
