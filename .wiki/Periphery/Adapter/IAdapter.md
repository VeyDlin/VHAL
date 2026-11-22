# IAdapter

Base interface for all VHAL hardware adapters. Every peripheral adapter (UART, SPI, GPIO, etc.) inherits from `IAdapter` to get a unified initialization lifecycle.

Header: `#include <Adapter/IAdapter.h>`

## Initialization Lifecycle

When an adapter's `SetParameters()` is called, the following sequence runs:

1. `BeforeInitialization()` — calls `beforePeripheryInit` callback if set
2. `Initialization()` — pure virtual, implemented by each platform adapter (register-level init)
3. `AfterInitialization()` — calls `afterPeripheryInit` callback if set

## Callbacks

| Callback | Type | Description |
|----------|------|-------------|
| `beforePeripheryInit` | `std::function<ResultStatus()>` | Called before hardware registers are configured. Use this to enable clocks, configure GPIO alternate functions, and set up NVIC interrupts |
| `afterPeripheryInit` | `std::function<ResultStatus()>` | Called after hardware initialization completes. Use this for post-init configuration |

## Changing a callback while its interrupt may run

A callback that an interrupt can call has a setter named `Set` plus the field name, for example `SetOnInterrupt(handler)`. The field stays public, so code that assigns it still compiles. Each adapter page that has setters lists them.

Use the setter to replace a callback while its interrupt may run:

- The setter holds interrupts off for one move of the new callable into the field. The field is written only inside that section.
- The replaced callable is destroyed after the section is left, so its destructor does not run with interrupts off.
- A plain assignment to the field is safe only while the interrupt cannot fire, for example before the interrupt is enabled.
- Do not replace a callback from an interrupt of higher priority than the one that calls it. The replaced callable may be running in the lower-priority handler, and destroying it there is not safe.
- Call a setter from thread context, not from an interrupt. The replaced callable is destroyed in the caller's context, after the section is left, so a heap-backed callable is freed there.
- A callback must not replace itself. A handler that calls its own setter, for example `SetOnTransferComplete(nullptr)` to disarm itself, destroys the closure it is still running in. Set a flag in the handler instead, and clear the callback from thread context.
- A setter may be called inside a critical section that is already open. The sections nest.
- On ESP32 the section masks interrupts on the calling core only. A callback whose interrupt runs on the other core may be swapped only while that interrupt cannot fire, or from the core that installed the driver.

Callbacks that run only in a thread have no setter. These are the `beforePeripheryInit` and `afterPeripheryInit` hooks, the `interruptPeripheryInit` and `eventPeripheryInit` hooks, and the I2C mutex hook.

## BSP Configuration Example

The `beforePeripheryInit` callback is the primary mechanism for BSP (Board Support Package) integration. It runs automatically when the adapter initializes, so you configure clocks, pins, and interrupts once:

```cpp
// BSP.cpp
void BSP::InitAdapterPeripheryEvents() {
    consoleSerial.beforePeripheryInit = []() {
        // 1. Enable peripheral clock
        LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);

        // 2. Configure GPIO for UART (TX = PA9, RX = PA10, AF1)
        AGPIO::AlternateInit({ GPIOA, 9,  1, AGPIO::Pull::Up, AGPIO::Speed::VeryHigh });
        AGPIO::AlternateInit({ GPIOA, 10, 1, AGPIO::Pull::Up, AGPIO::Speed::VeryHigh });

        // 3. Enable interrupt
        NVIC_SetPriority(USART1_IRQn, 0);
        NVIC_EnableIRQ(USART1_IRQn);

        return ResultStatus::ok;
    };
}
```

## Creating a Custom Adapter

All platform adapters must implement the pure virtual `Initialization()` method:

```cpp
template<typename HandleType>
class MyPeripheralAdapter : public IAdapter {
protected:
    ResultStatus Initialization() override {
        auto status = BeforeInitialization();
        if (status != ResultStatus::ok) return status;

        // Platform-specific register configuration here

        return AfterInitialization();
    }
};
```
