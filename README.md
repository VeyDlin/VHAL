# VHAL

[![Documentation](https://img.shields.io/badge/docs-veydlin.github.io%2FVHAL-blue)](https://veydlin.github.io/VHAL/)

VHAL is a C++20 multiplatform HAL library for embedded MCUs. Write your application logic once — switch between STM32, ESP32, or custom silicon by changing one config file.

Supported platforms: **STM32**, **ESP32**, **ENS001**

VHAL is not a code generator. It is a set of portable C++ abstractions — adapters, utilities, and an OS layer — that you compose into real-time applications with full control over hardware.

## Documentation
[WIKI](https://veydlin.github.io/VHAL/)

## Why VHAL

- **Portable peripheral adapters** — a common API for UART, SPI, I2C, ADC, TIM, GPIO, DMA, DAC, and more. Switch MCU families without rewriting application code.
- **RTOS abstraction** — threads, mutexes, events, signals, and critical sections via a clean C++ API. Works on top of FreeRTOS with chrono literals (`Sleep(500ms)`).
- **Reusable utilities** — math, animation, data structures, encoding, serialization, drivers, and hardware helpers — all platform-independent and ready to use.
- **Zero-cost BSP pattern** — adapters don't know about pins, clocks, or interrupts. Your BSP injects that knowledge through `beforePeripheryInit` callbacks, keeping application code portable.

## Quick Example

This application fragment reads an ADC value and updates a PWM output. It assumes a board-specific `BSP.h` and an initialized RTOS. It is not a complete board project: the BSP must configure clocks, pins, interrupts, ADC regular channel selection/calibration, and the timer output-compare channel before this code runs. See the projects in `.demo/` for board startup and build configuration.

### ADC to PWM

`ATIM::Channel::C1` is the STM32G0/G4 channel spelling. Check your selected port before reusing a channel identifier.

```cpp
#include <BSP.h>
#include <Adapter/Helper/TIM/TIMOutputCompareHelper.h>
#include <Utilities/Data/Colors/Colors.h>


// Call only after BSP::adc and BSP::pwmTimer have been configured.
void UpdateLedFromAdc(TIMOutputCompareHelper<float>& pwm) {
    Result<uint16> result = BSP::adc.Read<uint16>();
    if (!result.IsOk()) {
        return;
    }

    // Assumes a 12-bit ADC; the PWM helper accepts duty in percent.
    float level = static_cast<float>(result.Value()) / 4095.0f;
    Colors::GammaProfile gamma = Colors::GammaProfile::sRGB();
    Colors::FRgb corrected = gamma.Apply(Colors::FRgb(level, level, level));
    pwm.SetDuty(corrected.r * 100.0f);
}


// Run this from an application task after board initialization.
void RunLedTask() {
    TIMOutputCompareHelper<float> pwm(BSP::pwmTimer, ATIM::Channel::C1);
    pwm.SetFrequencyInfo({ .frequencyHz = 20000.0f, .duty = 0.0f });
    pwm.SetState(true);
    pwm.EnableCounter(true);

    while (true) {
        UpdateLedFromAdc(pwm);
        OS::IThread::Sleep(std::chrono::milliseconds(50));
    }
}
```

For eased transitions, use [Animation](https://veydlin.github.io/VHAL/docs/Common/Utilities/Animation/) with `float` or an IQ fixed-point type. The PWM helper currently clamps a zero compare value to one tick; use the port's output-disable mechanism when a guaranteed off state is required.

### Register-backed commands

Register entries must be attached through the public `RegisterData(...)` method. `LinkRegisterData(...)` is an internal, protected hook. A map update must contain the full payload for the selected register; receiving one UART byte is not a complete register-write operation.

```cpp
#include <Utilities/Data/RegisterMap/RegisterMap.h>


RegisterMap<uint8, 8, 64> registers;
RegisterData<0x02, uint16> brightnessReg;


void InitRegisters() {
    brightnessReg.SetEvents([](const uint16& value) -> bool {
        return value <= 4095;
    });
    registers.RegisterData(brightnessReg);
}


bool SetBrightness(uint16 value) {
    return brightnessReg.Set(value);
}
```

Call `InitRegisters()` once before accessing the entry. UART framing, byte-order conversion, and scheduling belong to the application or a protocol driver; they are intentionally omitted here. For a framed transport, see [ReliableProtocolCOBS](https://veydlin.github.io/VHAL/docs/Common/Drivers/Interface/User/ProtocolCOBS/). Register callbacks run in the caller's context and must synchronize any shared application state.

## Architecture

```
Application (your code)
    │
    ├── Utilities (Animation, IQ Math, Colors, RegisterMap, Console, ...)
    │
    ├── OS Layer (Thread, Mutex, Event, CriticalSection)
    │
    └── Adapters (UART, SPI, I2C, ADC, TIM, GPIO, ...)
            │
            └── Port (STM32G0, STM32G4, STM32F4, ENS001, ESP32)
                    │
                    └── Hardware registers
```

- **Adapters** define the API — `UARTAdapter<HandleType>` is a pure interface
- **Ports** implement the API — `UARTAdapterG0` writes to STM32G0 registers
- **BSP** wires pins, clocks, and interrupts — the only non-portable layer
- **Application** code never touches registers directly

## Supported Platforms

The table lists adapters selected by the current port headers, not a hardware validation matrix. Availability depends on the selected chip, build flags, and implemented operations; check returned status values for unsupported features.

| Platform | Adapters |
|----------|----------|
| STM32G0 | UART, I2C, ADC, TIM, GPIO, DAC, DMA, IWDG |
| STM32G4 | UART, ADC, TIM, GPIO, DAC, DMA, IWDG, COMP |
| STM32F4 | UART, SPI, I2C, ADC, TIM, GPIO, DAC, IWDG, FLASH |
| ENS001 | UART, SPI, I2C, ADC, TIM, GPIO, IWDG, FLASH, COMP, WaveGenerator, Boost, PGA, PMU |
| ESP32 | UART, SPI, I2C, ADC, DAC, I2S, GPIO, GPTimer, LEDC, MCPWM, DSI, PPA, LDO |

## Getting Started

1. Install the required tools (CMake, Ninja, ARM GCC, OpenOCD)
2. Clone the repository with submodules
3. Open a demo project in `.demo/` with VS Code
4. Build and flash — `F5` to debug
