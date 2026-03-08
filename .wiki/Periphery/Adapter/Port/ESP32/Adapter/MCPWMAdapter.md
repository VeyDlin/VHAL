# MCPWMAdapter

Abstract adapter for the ESP32 MCPWM (Motor Control PWM) peripheral. Provides advanced PWM with multiple comparators, generators, dead-time insertion, and brake support. Designed for motor control, H-bridge drivers, and power converter applications.

Header: `#include <Adapter/Port/ESP32/Adapter/MCPWMAdapter.h>`

> **Note:** This adapter does not use a template parameter — ESP-IDF manages MCPWM handles internally.

## Option Types

| Type | Base | Description |
|------|------|-------------|
| `CountModeOption` | `IOption<uint32>` | Counter mode (up, down, up-down) |

## Parameters

```cpp
struct Parameters {
    uint32 resolutionHz = 1000000;
    uint32 periodTicks = 20000;
    CountModeOption countMode;
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `resolutionHz` | `1000000` | Timer resolution (determines minimum period granularity) |
| `periodTicks` | `20000` | PWM period in ticks |
| `countMode` | — | Counter direction (platform-specific values) |

## Generator & Dead-Time Config

```cpp
struct GeneratorConfig {
    int pin = -1;
};

struct DeadTimeConfig {
    uint32 posedgeDelayTicks = 0;
    uint32 negedgeDelayTicks = 0;
};
```

## Constructor

```cpp
MCPWMAdapter(int groupId);
```

## API

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(Parameters val)` | `ResultStatus` | Configure timer |
| `AddComparator(uint32 compareValue)` | `ResultStatus` | Add a comparator with initial value |
| `AddGenerator(GeneratorConfig config, uint8 comparatorIndex)` | `ResultStatus` | Bind a generator to a GPIO and comparator |
| `SetCompare(uint8 comparatorIndex, uint32 value)` | `ResultStatus` | Update comparator value |
| `SetDutyPercent(uint8 comparatorIndex, float percent)` | `ResultStatus` | Set duty as percentage |
| `SetDeadTime(uint8 genA, uint8 genB, DeadTimeConfig config)` | `ResultStatus` | Insert dead-time between complementary outputs |
| `Start()` | `ResultStatus` | Start PWM |
| `Stop()` | `ResultStatus` | Stop PWM |
| `GetPeriodTicks()` | `uint32` | Get configured period |

## Callbacks

| Callback | Type | Description |
|----------|------|-------------|
| `onBrake` | `std::function<void()>` | Brake event triggered |

## Usage Example

```cpp
MCPWMAdapter motorPwm(0); // Group 0

motorPwm.SetParameters({
    .resolutionHz = 1000000,
    .periodTicks = 20000, // 50Hz for servo
    .countMode = MCPWMAdapter::CountMode::Up
});

// Add comparator and two generators for H-bridge
motorPwm.AddComparator(1500); // 1.5ms pulse (center position)
motorPwm.AddGenerator({ .pin = 15 }, 0); // PWM A
motorPwm.AddGenerator({ .pin = 16 }, 0); // PWM B

// Add dead-time to prevent shoot-through
motorPwm.SetDeadTime(0, 1, {
    .posedgeDelayTicks = 100,
    .negedgeDelayTicks = 100
});

motorPwm.Start();

// Change servo position
motorPwm.SetCompare(0, 1000);  // 1ms → full left
motorPwm.SetCompare(0, 2000);  // 2ms → full right

// Or use percentage
motorPwm.SetDutyPercent(0, 50.0f); // 50% duty
```
