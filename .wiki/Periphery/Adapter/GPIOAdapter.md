# GPIOAdapter

Abstract adapter for GPIO (General Purpose I/O) pins. Supports input, output, open-drain, alternate function, analog, interrupt, and event modes. Provides logic inversion, chainable API, and static helper methods for pin initialization.

Header: `#include <Adapter/GPIOAdapter.h>`

## Template Parameter

```cpp
template<typename PortType = void>
class GPIOAdapter : public IAdapter;
```

`PortType` — platform GPIO port type (e.g., `GPIO_TypeDef` on STM32). Use `void` for platforms without port registers (ESP32).

The `hasPort` constant (`!std::is_void_v<PortType>`) controls port-dependent constructors and methods at compile time.

## Parameters

```cpp
struct Parameters {
    Mode mode = Mode::Input;
    Pull pull = Pull::None;
    Speed speed = Speed::Low;
};
```

| Field | Options | Default | Description |
|-------|---------|---------|-------------|
| `mode` | `Input`, `Output`, `OpenDrain`, `InterruptRising/Falling/RisingFalling`, `EventRising/Falling/RisingFalling`, `Alternate`, `AlternateOpenDrain`, `Analog` | `Input` | Pin function |
| `pull` | `Up`, `Down`, `None` | `None` | Internal pull resistor |
| `speed` | `Low`, `Medium`, `High`, `VeryHigh` | `Low` | Output slew rate |

## Helper Structs

```cpp
struct AlternateParameters {
    PortType *port;
    uint8 pin;
    uint8 alternate = 0;
    Pull pull = Pull::None;
    Speed speed = Speed::Low;
};

struct AnalogParameters {
    PortType *port;
    uint8 pin;
    Pull pull = Pull::None;
};

struct IO {
    PortType *port;
    uint8 pin;
};
```

## Constructors

```cpp
GPIOAdapter(PortType *gpioPort, uint8 gpioPin, bool gpioInversion = false);
GPIOAdapter(IO &io);
GPIOAdapter(uint8 gpioPin, bool gpioInversion = false); // only when PortType = void
```

## Output API

| Method | Return | Description |
|--------|--------|-------------|
| `SetState(bool state)` | `GPIOAdapter&` | Set pin level (respects inversion) |
| `Set()` | `GPIOAdapter&` | Set pin high (logical) |
| `Reset()` | `GPIOAdapter&` | Set pin low (logical) |
| `Toggle()` | `GPIOAdapter&` | Toggle pin state |
| `operator=(bool state)` | `void` | Assign state directly |

## Input API

| Method | Return | Description |
|--------|--------|-------------|
| `GetState()` | `bool` | Read pin level (respects inversion) |
| `GetByteState(uint8 offset)` | `uint8` | Read pin as bit at given offset |
| `Wait(bool state)` | `GPIOAdapter&` | Busy-wait until pin matches state |

## Configuration & Control

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(Parameters val)` | `ResultStatus` | Apply pin configuration |
| `SetAlternate(uint8 val)` | `GPIOAdapter&` | Set alternate function number |
| `SetInversion(bool val)` | `void` | Set logic inversion |
| `IsInversion()` | `bool` | Get inversion state |
| `GetPin()` | `uint32` | Get pin bitmask |
| `GetPort()` | `PortType*` | Get port pointer (only if `hasPort`) |
| `IrqHandler()` | `void` | Call from GPIO EXTI IRQ |

## Static Init Helpers

These methods configure a temporary GPIO for a peripheral's alternate function:

```cpp
// Standard alternate (push-pull)
AGPIO::AlternateInit({ GPIOA, 9, 1, AGPIO::Pull::Up, AGPIO::Speed::VeryHigh });

// Open-drain alternate (for I2C)
AGPIO::AlternateOpenDrainInit({ GPIOB, 6, 6, AGPIO::Pull::Up, AGPIO::Speed::VeryHigh });

// Analog (for ADC/DAC)
AGPIO::AnalogInit({ GPIOA, 0 });
```

## Callbacks

| Callback | Type | Description |
|----------|------|-------------|
| `onInterrupt` | `std::function<void(bool state)>` | Called on pin interrupt (passes current state) |
| `interruptPeripheryInit` | `std::function<ResultStatus()>` | Custom interrupt initialization |
| `eventPeripheryInit` | `std::function<ResultStatus()>` | Custom event initialization |

## BSP Configuration Example

```cpp
// BSP.h
class BSP {
public:
    static AGPIO ledPin;
    static AGPIO buttonPin;
};

// BSP.cpp
AGPIO BSP::ledPin = { GPIOC, 6, true };      // inverted (active low)
AGPIO BSP::buttonPin = { GPIOA, 0, false };
```

## Usage Example

```cpp
// Configure LED
BSP::ledPin.SetParameters({
    .mode = AGPIO::Mode::Output,
    .pull = AGPIO::Pull::None,
    .speed = AGPIO::Speed::Medium
});

// Chainable API
BSP::ledPin.Set().Wait(true).Reset();

// Interrupt button
BSP::buttonPin.SetParameters({
    .mode = AGPIO::Mode::InterruptFalling,
    .pull = AGPIO::Pull::Up
});

BSP::buttonPin.onInterrupt = [](bool state) {
    BSP::ledPin.Toggle();
};
```
