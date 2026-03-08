# VHAL

[![Documentation](https://img.shields.io/badge/docs-veydlin.github.io%2FVHAL-blue)](https://veydlin.github.io/VHAL/)

VHAL is a C++20 multiplatform HAL library for embedded MCUs. Write your application logic once — switch between STM32, ESP32, or custom silicon by changing one config file.

Supported platforms: **STM32**, **ESP32**, **ENS001**

VHAL is not a code generator. It is a set of portable C++ abstractions — adapters, utilities, and an OS layer — that you compose into real-time applications with full control over hardware.

## Why VHAL

- **Portable peripheral adapters** — a common API for UART, SPI, I2C, ADC, TIM, GPIO, DMA, DAC, and more. Switch MCU families without rewriting application code.
- **RTOS abstraction** — threads, mutexes, events, signals, and critical sections via a clean C++ API. Works on top of FreeRTOS with chrono literals (`Sleep(500ms)`).
- **Reusable utilities** — math, animation, data structures, encoding, serialization, drivers, and hardware helpers — all platform-independent and ready to use.
- **Zero-cost BSP pattern** — adapters don't know about pins, clocks, or interrupts. Your BSP injects that knowledge through `beforePeripheryInit` callbacks, keeping application code portable.

## Quick Example

A complete application that reads an ADC voltage, smoothly animates an LED brightness to match, applies gamma correction for a natural brightness curve, and accepts commands over UART via a register map.

### BSP — hardware wiring

```cpp
// BSP.h
#pragma once
#include <VHAL.h>

class BSP {
public:
    static AUART  serial;
    static AADC   adc;
    static ATIM   pwmTimer;
    static AGPIO  ledPin; // status LED (optional)

    static void Init();

private:
    static void InitClock();
    static void InitSystemTick(uint32 ms, uint32 tickPriority);
    static void InitAdapterPeripheryEvents();
};
```

```cpp
// BSP.cpp
#include "BSP.h"

AUART BSP::serial   = { USART1 };
AADC  BSP::adc      = { ADC1 };
ATIM  BSP::pwmTimer = { TIM1, SystemCoreClock };
AGPIO BSP::ledPin   = { GPIOC, 6 };

void BSP::Init() {
    InitClock();
    InitSystemTick(1, 0);
    System::Init();
    InitAdapterPeripheryEvents();
}

void BSP::InitAdapterPeripheryEvents() {
    serial.beforePeripheryInit = []() {
        LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);
        AGPIO::AlternateInit({ GPIOA, 9,  1, AGPIO::Pull::Up, AGPIO::Speed::VeryHigh }); // TX
        AGPIO::AlternateInit({ GPIOA, 10, 1, AGPIO::Pull::Up, AGPIO::Speed::VeryHigh }); // RX
        NVIC_SetPriority(USART1_IRQn, 0);
        NVIC_EnableIRQ(USART1_IRQn);
        return ResultStatus::ok;
    };

    adc.beforePeripheryInit = []() {
        LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_ADC);
        AGPIO::AnalogInit({ GPIOA, 0 });
        NVIC_SetPriority(ADC1_IRQn, 2);
        NVIC_EnableIRQ(ADC1_IRQn);
        return ResultStatus::ok;
    };

    pwmTimer.beforePeripheryInit = []() {
        LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_TIM1);
        AGPIO::AlternateInit({ GPIOA, 8, 2, AGPIO::Pull::None, AGPIO::Speed::VeryHigh });
        return ResultStatus::ok;
    };
}
```

```cpp
// IRQ/DeviceIrq.cpp
#include "BSP.h"

void USART1_IRQHandler() { BSP::serial.IrqHandler(); }
void ADC1_IRQHandler()   { BSP::adc.IrqHandler(); }
void TIM1_CC_IRQHandler(){ BSP::pwmTimer.IrqHandler(); }
```

### Application — the interesting part

```cpp
#include <Application.h>
#include <BSP.h>
#include <AnimationRTOS.h>
#include <Colors.h>
#include <Math/IQMath/IQ.h>
#include <Data/RegisterMap/RegisterMap.h>
#include <Adapter/Helper/TIM/TIMOutputCompareHelper.h>

using namespace OS;
using namespace Colors;

using iq = IQ<16>;


class MainTask : public ThreadStatic<512> {
    // PWM helper — IQ fixed-point math for MCUs without FPU
    TIMOutputCompareHelper<iq> pwm;

    // Animation — runs on its own RTOS thread, auto-updates at ~60 FPS
    AnimationRTOS<iq, 128> brightness;

    // Gamma correction — converts linear brightness to perceptually correct PWM
    GammaProfile gamma = GammaProfile::sRGB();

    // Register map — serial command interface
    RegisterMap<uint8, 8, 64> registers;
    RegisterData<0x01, uint16> adcValueReg;       // Read: current ADC value
    RegisterData<0x02, uint16> brightnessReg;      // Write: target brightness (0..4095)
    RegisterData<0x03, uint8>  animSpeedReg;       // Write: animation speed (ms / 10)

    uint16 currentAdcValue = 0;
    uint16 animDurationMs = 500;


    void Execute() override {
        InitPeripherals();
        InitAnimation();
        InitRegisterMap();
        InitSerialReceive();

        while (true) {
            // Read ADC voltage
            auto result = BSP::adc.Read<uint16>();
            if (result.IsOk()) {
                currentAdcValue = result.Value();
                adcValueReg.Set(currentAdcValue);

                // Map 12-bit ADC (0..4095) → brightness (0..1)
                iq target = iq(currentAdcValue) / 4095;
                brightness.Tween(target, std::chrono::milliseconds(animDurationMs));
            }

            Sleep(50ms);
        }
    }


    void InitPeripherals() {
        // UART
        BSP::serial.SetParameters({ .baudRate = 115200 });

        // ADC
        BSP::adc.SetParameters({
            .resolution = AADC::Resolution::B12,
            .dataAlignment = AADC::DataAlignment::Right
        });
        BSP::adc.ConfigRegularGroup(
            { .continuousMode = AADC::ContinuousMode::Single },
            { { .channel = 0, .maxSamplingTimeNs = 5000 } }
        );
        BSP::adc.Calibration();

        // PWM — 20 kHz via helper
        pwm = { BSP::pwmTimer, ATIM::Channel::Ch1 };
        pwm.SetFrequencyInfo({ .frequencyHz = iq(20000), .duty = iq(0) });
        pwm.SetState(true);
    }


    void InitAnimation() {
        brightness.SetEasing(Easing<iq>::Curve::EaseInOut);

        brightness.onUpdateValue = [this](iq linear) {
            // Apply gamma: IQ → float for color lib → back to IQ for PWM
            FRgb corrected = gamma.Apply(FRgb(linear.ToFloat(), linear.ToFloat(), linear.ToFloat()));

            // Set PWM duty from corrected brightness
            pwm.SetDuty(iq(corrected.r * 100.0f));
        };
    }


    void InitRegisterMap() {
        adcValueReg.ReadOnly();
        registers.LinkRegisterData(&adcValueReg);

        brightnessReg.SetEvents([this](const uint16& val) {
            // External command: animate to specific brightness
            iq target = iq(val) / 4095;
            brightness.Tween(target, std::chrono::milliseconds(animDurationMs));
            return true;
        }, nullptr);
        registers.LinkRegisterData(&brightnessReg);

        animSpeedReg.SetEvents([this](const uint8& val) {
            animDurationMs = val * 10;
            return true;
        }, nullptr);
        registers.LinkRegisterData(&animSpeedReg);
    }


    void InitSerialReceive() {
        BSP::serial.onInterrupt = [this](AUART::Irq irq) {
            if (irq == AUART::Irq::Rx) {
                uint8 byte = BSP::serial.GetLastRxData();
                // Simple protocol: [address] [data_high] [data_low]
                registers.UpdateMemory(byte, &byte, 1);
            }
        };
        BSP::serial.SetContinuousAsyncRxMode(true);
    }
};
```

### What this demonstrates

| Feature | How it's used |
|---------|---------------|
| **RTOS thread** | `ThreadStatic<512>` with `Sleep()`, `Execute()` loop |
| **ADC with interrupts** | Async conversion, `IrqHandler()` delegation |
| **IQ fixed-point math** | `IQ<16>` — all calculations without FPU, seamless with Animation and TIM helper |
| **Timer + PWM helper** | `TIMOutputCompareHelper<iq>` — frequency/duty in one call, no manual register math |
| **AnimationRTOS + easing** | `AnimationRTOS<iq, 128>` — runs on its own RTOS thread, auto-updates at ~60 FPS |
| **Gamma correction** | `GammaProfile::sRGB()` for perceptually linear LED brightness |
| **UART with interrupts** | Continuous async RX mode, `onInterrupt` callback |
| **RegisterMap** | Expose ADC value as readable register, accept brightness/speed commands as writable registers |

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

| Platform | Status | Adapters |
|----------|--------|----------|
| STM32G0 | Production | UART, SPI, I2C, ADC, TIM, GPIO, DAC, DMA, IWDG, FLASH, COMP |
| STM32G4 | Production | UART, SPI, I2C, ADC, TIM, GPIO, DAC, DMA, IWDG, COMP |
| STM32F4 | Production | UART, SPI, I2C, ADC, TIM, GPIO, DAC, DMA, IWDG, I2S, DSI |
| ENS001 | Production | UART, SPI, I2C, ADC, TIM, GPIO, IWDG, FLASH, COMP, WaveGenerator, Boost, PGA, PMU |
| ESP32 | In progress | UART, SPI, I2C, GPIO, GPTimer, LEDC, MCPWM |

## Getting Started

1. Install the required tools (CMake, Ninja, ARM GCC, OpenOCD)
2. Clone the repository with submodules
3. Open a demo project in `.demo/` with VS Code
4. Build and flash — `F5` to debug
