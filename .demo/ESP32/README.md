# Creating an ESP32 + VHAL Project (ESP-IDF)

This example uses `ESP32-C3`, but the instructions apply to any ESP32 chip supported by ESP-IDF.

Reference: `.demo/ESP32/`, a ready-made project with the same Application structure as `.demo/STM32G0/`. The main task prints a greeting, toggles an LED and prints `Blink` every second. The watchdog task is off by default (`Application::useWatchDog`).


## Requirements

| Tool | Purpose |
|---|---|
| **ESP-IDF** 5.5 | Toolchain, build system, FreeRTOS |


## Project Structure

```
MyProject/
├── VHAL/                          ← framework (submodule)
├── main.cpp                       ← entry point (app_main)
├── main/
│   └── CMakeLists.txt             ← ESP-IDF component: sources and include paths
├── BSP/
│   ├── BSP.h                      ← peripheral declarations
│   ├── BSP.cpp                    ← peripheral definitions and initialization
│   └── VHALConfig.h               ← VHAL configuration
├── Application/
│   ├── Application.h / .cpp
│   └── AppThreads/
│       ├── MainTask.h
│       └── McuWatchDogTask.h
├── CMakeLists.txt                 ← ESP-IDF project
└── sdkconfig.defaults
```

Inside the VHAL repository the demo uses the enclosing checkout. In your own project, place VHAL in `VHAL/` or pass its path: `idf.py -D VHAL_DIR=/path/to/VHAL build`.


## VHALConfig.h

```cpp
#define VHAL_ESP32
#define VHAL_ESP32_UART
#define VHAL_ESP32_IWDG

#define VHAL_RTOS
#define VHAL_RTOS_FREERTOS

#define VHAL_SYSTEM_CONSOLE
```

The list of `VHAL_ESP32_*` peripherals is in `.wiki/VHAL/3. VHAL Config.md`. Do not define `VHAL_RUNTIME` or `VHAL_SYSTEM_CONSOLE_RUNTIME`: ESP-IDF provides its own runtime.


## main.cpp

```cpp
#include <Application.h>


extern "C" void app_main() {
	BSP::Init();
	Application::Init();
}
```


## Differences from the STM32 demo

- ESP-IDF sets up the clock and starts FreeRTOS before `app_main()`. `BSP::Init()` has no `InitSystem`/`InitClock`, and `Application::Init()` does not call `RTOS::Start()`.
- The VHAL system tick (`System::TickHandler()`) comes from a 1 ms `esp_timer` in `BSP::InitSystemTick()`.
- `System::Init()` already installs `System::rtosDelayMsHandle` on ESP32, so `Application::InitSystemHandle()` sets only `criticalErrorHandle`.
- Task stacks are in bytes under ESP-IDF, so `ThreadStatic` sizes are larger than in the STM32 demo.
- `AIWDG` is the ESP-IDF task watchdog (TWDT). `Start()` subscribes the calling task, so `Reset()` must be called from that task.
- FreeRTOS comes from ESP-IDF. Do not compile VHAL's FreeRTOS kernel; `main/CMakeLists.txt` adds the ESP-IDF FreeRTOS include paths that VHAL expects.
- The ESP-IDF console is off (`CONFIG_ESP_CONSOLE_NONE`), so `AUART` owns UART0.


## Board

Defaults in `BSP/BSP.cpp`: LED on GPIO4 (active high, external LED with a series resistor), UART0 TX on GPIO21 and RX on GPIO20 at 115200 baud. Connect a 3.3 V USB-UART adapter to see the output. For another board, change the pins in `BSP.cpp`; for another chip, run `idf.py set-target <chip>`.


## Build and Flash

```sh
cd .demo/ESP32
idf.py build
idf.py -p PORT flash monitor
```
