# BatteryLevel

`template <RealType Type = float>`

A utility class for estimating battery state of charge (SoC) from voltage using a custom discharge profile with hysteresis filtering.

## Table of Contents

- [Quick Start](#quick-start)
- [BatteryProfilePoint](#batteryprofilepoint)
- [Constructor](#constructor)
- [GetDivision](#getdivision)
- [Multi-Cell Batteries](#multi-cell-batteries)
- [API Reference](#api-reference)

**Dependencies:**
- `Hysteresis<Type>` -- used to filter voltage input and avoid flickering between divisions
- `MathUtilities::Extrapolation` -- linear extrapolation between profile points

## Quick Start

```cpp
#include <Utilities/Hardware/BatteryLevel/BatteryLevel.h>

// Define a Li-Ion single-cell profile (voltage descending, level descending)
BatteryLevel battery({
    {4.20f, 100},
    {3.80f,  90},
    {3.55f,  20},
    {3.10f,  10},
    {3.00f,   0}
});

// Read ADC voltage and get the current charge division (0..10)
float voltage = ReadBatteryVoltage();
uint8 division = battery.GetDivision(voltage);
```

## BatteryProfilePoint

A simple struct describing one point on the battery discharge curve.

```cpp
struct BatteryProfilePoint {
    Type voltage;   // battery voltage at this point
    uint8 level;    // charge level in percent (0-100)
};
```

Points must be ordered from **highest voltage/level to lowest**. The class walks the array from the bottom up and uses linear extrapolation between adjacent points.

## Constructor

```cpp
BatteryLevel(
    const std::initializer_list<BatteryProfilePoint>& batteryProfile,
    uint8 divisionsQuantity = 10,
    uint8 cellsQuantity    = 1,
    Type jitter             = Type(0)
);
```

| Parameter | Description |
|---|---|
| `batteryProfile` | Voltage-to-level profile points (highest voltage first) |
| `divisionsQuantity` | Number of discrete charge divisions returned by `GetDivision` (default: 10) |
| `cellsQuantity` | Number of series cells in the pack (default: 1) |
| `jitter` | Hysteresis band in volts. If `0` or negative, auto-calculated as half the voltage span of one division |

**Example -- 2-cell pack with 5 divisions:**

```cpp
BatteryLevel battery({
    {4.20f, 100},
    {3.80f,  75},
    {3.50f,  30},
    {3.00f,   0}
}, 5, 2);

// Pass the total pack voltage (2 cells in series)
uint8 div = battery.GetDivision(7.6f); // internally divides by 2
```

**Auto-calculated jitter:** With profile voltages 4.2 V and 3.0 V, 1 cell, and 10 divisions, the jitter is `(4.2 - 3.0) / 10 / 2 = 0.06 V`.

## GetDivision

```cpp
uint8 GetDivision(float batteryVoltage);
```

Returns the current charge division as an integer from `0` (empty) to `divisions` (full).

The method:

1. Applies hysteresis filtering to the raw voltage.
2. Divides by the number of cells to get per-cell voltage.
3. Clamps to `divisions` if voltage is at or above the top profile point, or `0` if at or below the bottom.
4. Otherwise, linearly extrapolates between the two surrounding profile points to get a percentage, then maps it to a division index.

```cpp
BatteryLevel battery({
    {4.20f, 100},
    {3.80f,  90},
    {3.55f,  20},
    {3.10f,  10},
    {3.00f,   0}
});

battery.GetDivision(4.20f); // 10  (full)
battery.GetDivision(3.90f); // 9   (between 90-100%)
battery.GetDivision(3.00f); // 0   (empty)
battery.GetDivision(2.50f); // 0   (below minimum)
battery.GetDivision(5.00f); // 10  (above maximum)
```

## Multi-Cell Batteries

When `cellsQuantity > 1`, pass the **total pack voltage**. The class internally divides it by the cell count before looking up the profile.

```cpp
// 3S Li-Ion pack
BatteryLevel battery3S({
    {4.20f, 100},
    {3.70f,  50},
    {3.00f,   0}
}, 10, 3);

// Pack voltage = 11.1 V (3 * 3.7 V) -> per-cell 3.7 V -> ~50% -> division 5
uint8 div = battery3S.GetDivision(11.1f);
```

## API Reference

### `BatteryLevel` class

| Member | Type | Description |
|---|---|---|
| `BatteryLevel(profile, divisions, cells, jitter)` | constructor | Creates battery model from a voltage profile |
| `GetDivision(Type batteryVoltage)` | `uint8` | Returns charge division (0 to `divisions`) |

### `BatteryProfilePoint` struct

| Field | Type | Description |
|---|---|---|
| `voltage` | `Type` | Per-cell voltage at this profile point |
| `level` | `uint8` | Charge level in percent (0-100) |
