# ENS001 — Demo Project for Neurostimulation SoC

Demo project for the **ENS001** microcontroller (ARM Cortex-M0, Hangzhou Nanochap) based on the [VHAL](../../README.md) framework.

Compatible with ENS001-A, ENS001-A2, and ENS001-A3 variants — peripheral registers are identical.


## Requirements

| Tool | Purpose | Link |
|---|---|---|
| **CMake** ≥ 3.20 | Build system | https://cmake.org/download/ |
| **arm-none-eabi-gcc** | ARM cross-compiler | https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads |
| **Ninja** | Build generator | https://ninja-build.org/ |

Verify installation:
```
cmake --version
arm-none-eabi-gcc --version
ninja --version
```


## Build

```bash
# Configuration
cmake --preset Debug

# Build
cmake --build --preset Debug

# Clean
cmake --build --preset Debug --target clean
```

Or use the VS Code status bar buttons (Build, Clean, Configure).

> **Configure** (`cmake --preset Debug`) — generates the build system (creates `build/Debug/`, `compile_commands.json`, `build.ninja`). Required on first project open or after modifying `CMakeLists.txt`. Usually runs automatically — `settings.json` has `"cmake.configureOnOpen": true`.

Build output:
- `build/Debug/ENS001.elf` — firmware
- `build/Debug/ENS001.bin` — binary image
- `build/Debug/ENS001.hex` — Intel HEX


## Flashing

Interface: **SWD** (Serial Wire Debug)
- SWCLK — GPIO0 (pin 84)
- SWDIO — GPIO1 (pin 85)

Programmer: ST-Link v2 or v3 via OpenOCD.


## Project Structure

```
ENS001/
├── Application/            Demo applications
│   ├── main.cpp            Entry point
│   ├── DemoBlink/          LED blinking
│   ├── DemoUART/           UART echo
│   ├── DemoWaveGen/        Sinusoidal stimulation
│   ├── DemoBoostADC/       Boost converter + ADC
│   └── DemoStim/            Electrical stimulation (VNS, TENS, EMS, IFT)
│       └── Modes/          Stimulation modes by application
├── BSP/                    Board Support Package
│   ├── BSP.h / BSP.cpp     Peripheral initialization
│   ├── VHALConfig.h        VHAL adapter configuration
│   └── IRQ/                Interrupt handlers
├── CMakeLists.txt          Build configuration
└── arm-none-eabi.cmake     Toolchain file
```

CMSIS, startup, and linker scripts are located in VHAL: `Adapter/Port/ENS/001/Drivers/` and `Adapter/Port/ENS/001/Linker/`.

> **Using in your own project**
>
> This project is a demo located inside the VHAL repository (`VHAL/.demo/ENS001/`).
> To use it in a real project:
>
> 1. Copy the `ENS001/` folder to a convenient location
> 2. Copy (or add as a submodule) VHAL into the project
> 3. In `CMakeLists.txt`, update the path to VHAL:
>    ```cmake
>    # Before (relative path to the parent VHAL):
>    set(VHAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
>
>    # After (VHAL inside the project):
>    set(VHAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/VHAL)
>    ```
> 4. Make sure the structure looks like this:
>    ```
>    MyProject/
>    ├── VHAL/              ← framework
>    ├── Application/
>    ├── BSP/
>    ├── CMakeLists.txt
>    └── arm-none-eabi.cmake
>    ```


## Demo Applications

Demo selection is in `Application/main.cpp`. Uncomment the desired `Run()` call.


### DemoBlink

LED blinking on GPIO6 with a 1-second period.

| Parameter | Value |
|---|---|
| Pin | GPIO6 |
| Period | 500 ms ON / 500 ms OFF |
| Peripheral | GPIO |


### DemoUART

Initializes UART0 (GPIO2 RX, GPIO3 TX), sends a greeting, and operates in echo mode — returns all received bytes.

| Parameter | Value |
|---|---|
| Baud rate | 115200 |
| Format | 8N1 |
| Peripheral | UART0, GPIO |


### DemoWaveGen

Generates a sinusoidal stimulation signal via WaveGen. 64-point sine wave with bipolar output. Cycles: 2 s stimulation, 1 s pause.

| Parameter | Value |
|---|---|
| Waveform | 64 points, sine |
| Half-wave period | 1000 us |
| Current selection | I3 (133 uA/unit) |
| Mode | ContinueRepeat |
| Peripheral | WaveGen |


### DemoBoostADC

Enables the boost DC-DC converter at 11 V and reads the ADC (12-bit, channel 1) every 100 ms.

| Parameter | Value |
|---|---|
| Boost voltage | 11 V |
| ADC resolution | 12 bit |
| ADC channel | 1 (GPIO21) |
| Peripheral | Boost, ADC, GPIO |


### DemoStim

Universal electrical stimulation demo. Accepts a `StimMode` structure with parameters:
- **Frequency** — pulse repetition frequency (Hz)
- **Pulse width** — duration of one phase of the bipolar pulse (us)
- **Current** — peak stimulation current (mA)
- **Duty cycle** — ON/OFF time for the macro cycle (ms)
- **Session** — total session duration (ms)

Charge balance is ensured by a bipolar pulse: the negative phase is 3 times longer than the positive phase at ~1/3 amplitude. Timer0 controls the pulse repetition frequency via ISR.

Available modes are organized by stimulation type in `Modes/`:

**VNS (vagus nerve stimulation):**

| File | Modes |
|---|---|
| `EpilepsyModes.h` | Classic, OptimalNorway, AuricularTragus |
| `DepressionModes.h` | Implantable, Auricular |
| `AnxietyModes.h` | Anxiolytic, HrvOptimal |
| `SleepModes.h` | Improvement, Insomnia |
| `AlertnessModes.h` | CervicalBurst*, Auricular |
| `PainModes.h` | MigraineBurst*, Chronic, RheumatoidArthritis |
| `InflammationModes.h` | AntiInflammatory |
| `AppetiteModes.h` | CervicalStimulation, AbdominalSatiety, FoodCueModulation |
| `TinnitusModes.h` | PairedSoundTherapy |
| `CognitiveModes.h` | Memory, WorkingMemory, DivergentThinking, SensoryPerception* |
| `RehabModes.h` | MotorRecovery |
| `NerveBlockModes.h` | AfferentBlock, DirectionalEfferent |

**TENS (transcutaneous electrical nerve stimulation):**

| File | Modes |
|---|---|
| `TENSModes.h` | Conventional, AcupunctureLike, Burst*, BriefIntense |

**EMS (electrical muscle stimulation):**

| File | Modes |
|---|---|
| `EMSModes.h` | Strengthening, Endurance, Recovery, Denervated |

**IFT (interferential therapy):**

| File | Modes |
|---|---|
| `IFTModes.h` | PainRelief, MuscleStim, Circulation |

\* — modes with `burstMode = true`, require a separate driver implementation.

Usage example:
```cpp
#include "DemoStim/DemoStim.h"
#include "DemoStim/Modes/AnxietyModes.h"

DemoStim::Run(StimModes::Anxiety::HrvOptimal);
```

Parameters for all modes are based on clinical research — details in [.documents/stim-parameters.md](.documents/stim-parameters.md).

Clock: HSI 32 MHz. SysTick: 1 ms.
