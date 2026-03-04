# VHAL
VHAL - C++20 multiplatform HAL library for embedded MCUs.

Supported platforms:
- **STM32** 
- **ENS001**

VHAL takes over the configuration of the periphery, all that is needed is to declare in BSP the necessary GPIOs and the periphery that will be used at the application level.

It should also be understood that VHAL does not allow the application to change the pinout (alternative pins) they are fixed in BSP.

## Platform selection

BSP provides a `VHALConfig.h` file (accessible via include path) with platform and peripheral defines. VHAL automatically includes the correct headers and port adapters based on these defines. See [VHALConfig](Documents/VHALConfig.md) for the full list of defines.

## Architecture

```
VHAL/
├── Periphery/
│   ├── Adapter/          # Base adapters (templates) + Port implementations
│   ├── System/           # Common system interface (tick, delay, device ID)
│   └── VHAL.h            # Entry point
└── Common/               # Platform-independent utilities
```

Base adapters are C++ templates parameterized by the peripheral register type. Each port provides a concrete adapter aliased via `using` for BSP:

```c++
// Base
template<typename PortType>
class GPIOAdapter : public IAdapter { ... };

// Port (e.g. STM32G0)
class GPIOAdapterG0 : public GPIOAdapter<GPIO_TypeDef> { ... };
using AGPIO = class GPIOAdapterG0;
```

## BSP setup

BSP requires manual initialization at the LL (registers) level:
- System clock configuration
- SysTick with `System::TickHandler()` and `RTOS::HandleSysTickInterrupt()`
- Peripheral clock enable, GPIO alternate mode, NVIC for interrupts

## Example

LED blinks on UART receive:

```c++
// BSP/BSP.h
class BSP {
public:
    static AGPIO ledPin;
    static AUART serial;
    // ...
};

// BSP/BSP.cpp
AGPIO BSP::ledPin = { GPIOC, 6 };
AUART BSP::serial = { USART1 };
```

```c++
// Application/ExampleTask.h
class ExampleTask: public OS::ThreadStatic<256> {
    void Execute() override {
        BSP::ledPin.SetParameters({ .mode = AGPIO::Mode::Output });

        uint8 rxBuffer[64];
        BSP::serial.SetParameters({ .baudRate = 115200 });
        BSP::serial.onInterrupt = [this](AUART::Irq irq) {
            if (irq == AUART::Irq::Rx) {
                Signal();
            }
        };
        BSP::serial.SetContinuousAsyncRxMode(true);
        BSP::serial.ReadArrayAsync(rxBuffer, sizeof(rxBuffer));

        while (true) {
            WaitForSignal();
            BSP::ledPin.Toggle();
        }
    }
};
```

```c++
// Application/Application.h
class Application {
public:
    static inline ExampleTask exampleTask;

    static void Init() {
        OS::RTOS::CreateThread(exampleTask, OS::ThreadPriority::normal, "example");
        OS::RTOS::Start();
    }
};
```

## Documentation

- [VHALConfig](Documents/VHALConfig.md) — Platform, peripheral and RTOS defines
- [STM32 Project Setup](Documents/STM32_ProjectSetup.md) — Creating a project in CubeIDE/CubeMX and configuring BSP
