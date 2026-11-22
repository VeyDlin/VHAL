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
| `IrqHandler()` | `void` | Call from the EXTI vector of the pin. On STM32, see [STM32 Ports](#stm32-ports) for the shared vectors |

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

Replace `onInterrupt` while its interrupt may run with its setter: `SetOnInterrupt(handler)`. See [Changing a callback while its interrupt may run](IAdapter.md#changing-a-callback-while-its-interrupt-may-run).

## ESP32 Port

`AGPIO` is `GPIOAdapterESP` (`Periphery/Adapter/Port/ESP32/Common/GPIOAdapterESP.h`). `PortType` is `void`, so the pin is given as a number:

```cpp
AGPIO(uint8 gpioPin, bool gpioInversion = false);
```

- `Output` and `OpenDrain` keep the input enabled, so `GetState()` returns the real pad level. `Toggle()` flips the last value written with `Set`/`Reset`/`SetState`, not the pad reading.
- `Mode::Alternate`, `Mode::AlternateOpenDrain` and the `Mode::Event*` modes return `ResultStatus::notSupported`.
- The three interrupt modes (`InterruptRising`, `InterruptFalling`, `InterruptRisingFalling`) register a handler through the shared ESP-IDF GPIO ISR service, which is installed on first use. `onInterrupt` runs in interrupt context.
- `interruptPeripheryInit` is optional on ESP32. When it is not set, initialization succeeds without calling it.
- A copied `AGPIO` does not take over the interrupt handler of the original. The copy starts without an attached interrupt; assignment detaches the interrupt of the target before copying.

Button interrupt:

```cpp
AGPIO button(0);

button.SetParameters({
    .mode = AGPIO::Mode::InterruptFalling,
    .pull = AGPIO::Pull::Up
});

button.onInterrupt = [](bool state) {
    // Interrupt context: keep the handler short and non-blocking
};
```

## STM32 Ports

`AGPIO` is `GPIOAdapterF4`, `GPIOAdapterG0` or `GPIOAdapterG4`, with `PortType` = `GPIO_TypeDef`.

Interrupt and event modes configure the EXTI line of the pin in `SetParameters()`:

- Port selection: `SYSCFG_EXTICR` on F4 and G4. The adapter enables the SYSCFG clock for it. On G0 the selection is in `EXTI_EXTICR`, which needs no clock.
- Edges from the mode: the rising edge for `InterruptRising`, `InterruptRisingFalling`, `EventRising` and `EventRisingFalling`; the falling edge for `InterruptFalling`, `InterruptRisingFalling`, `EventFalling` and `EventRisingFalling`. The other edge is disabled.
- Mask: an interrupt mode unmasks the interrupt, an event mode unmasks the event. The other mask stays off.
- The pending bit of the line is cleared before the line is enabled.
- One line, one port: an interrupt or event mode returns `notAvailable` when another port already uses the line. The pin is not changed in that case.
- Release: a pin that owns its line and moves to any other mode (normal, alternate or analog) clears the interrupt and event masks, the edge enables and the pending bit of the line. A pin on a line owned by another port leaves it alone.
- The adapter never touches the NVIC. An interrupt mode requires `interruptPeripheryInit` (which enables the NVIC line). An event mode requires `eventPeripheryInit`.

`IrqHandler()` serves only the line of this pin. It returns without a call when the line is not pending or the pin does not own the line. Otherwise it clears the pending bit (G0 keeps rising and falling pending bits apart, and both are cleared). It calls `onInterrupt` with the pin state only while the pin is in an interrupt mode.

Shared vectors: on F4 and G4 the lines 5 to 9 and 10 to 15 share `EXTI9_5_IRQn` and `EXTI15_10_IRQn`; on G0 the lines 4 to 15 share `EXTI4_15_IRQn`. The application's handler for such a vector calls `IrqHandler()` of every GPIO adapter on it. Each adapter serves its own line only, so a call for another line does nothing.

A pin in an event mode raises no interrupt, so its `IrqHandler()` is not needed.

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
