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
