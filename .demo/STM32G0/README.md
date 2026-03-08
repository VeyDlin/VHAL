# Creating an STM32 + VHAL Project (VS Code + CMake)

This example uses `STM32G071RBTx`, but the instructions apply to any STM32.

Reference: `.demo/STM32G0/` — a ready-made project for STM32G071, can be used as a template.


## Requirements

| Tool | Purpose |
|---|---|
| **CMake** ≥ 3.22 | Build system |
| **Ninja** | Build generator |
| **arm-none-eabi-gcc** | Cross-compiler for ARM |
| **STM32CubeMX** | Configuration generation (clocks, peripherals) |

VS Code extensions:
- [CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools) — UI for configuration/building
- [Tasks](https://marketplace.visualstudio.com/items?itemName=actboy168.tasks) — Build/Clean/Configure buttons in the status bar
- [Cortex-Debug](https://marketplace.visualstudio.com/items?itemName=marus25.cortex-debug) — debugging, flashing, peripheral register inspector


## Initial CubeMX Setup

CubeMX is used as a **donor** — you generate a project and then extract the configuration from it and transfer it to the VHAL structure. Later, if you need to add new peripherals or change settings, you simply regenerate the donor project and copy the necessary parts following these instructions.

1. Create a new project in CubeMX for your chip
2. Configure:
   - **Clock Configuration**
   - **SYS** → Timebase Source: **SysTick**. You can also use a timer, but make sure it is initialized first — some VHAL adapters use `System::TickHandler()` during initialization (for example, the ADC may use a delay for calibration)
   - **SYS** → Debug: **Serial Wire** (or Trace, if needed)
   - Enable the required **peripherals** and their **interrupts** (if you plan to use interrupts). No further configuration is needed — everything else is configured through VHAL
   - (optional) Enable **FreeRTOS** in `Middleware`. The CMSIS version does not matter — we only need the FreeRTOS files and for the project to be generated with RTOS support. This step can be skipped, but keep in mind that many VHAL drivers and utilities use RTOS
3. **Project Manager** → **Advanced Settings**: for each peripheral, switch **HAL** to **LL** (VHAL uses LL)
4. **Generate Code** — save the generated project

> The generated project is your **donor**. You will copy configuration from it in the following steps. It is convenient to place it directly in the project folder — for example, in `.cube/` (see `.demo/STM32G0/.cube/` as an example). This way the donor is always at hand, and you (or your colleagues) can find the original configuration.


## Creating a VHAL Project

### Project Structure

Create the project structure (or copy from `.demo/STM32G0/` and adapt):

```
MyProject/
├── .cube/                         ← CubeMX donor project (entire)
├── VHAL/                          ← framework (submodule)
├── main.cpp                       ← entry point
├── Core/
│   └── Inc/
│       └── FreeRTOSConfig.h       ← from donor (if FreeRTOS)
├── BSP/
│   ├── BSP.h                      ← peripheral declarations
│   ├── BSP.cpp                    ← initialization (from donor)
│   ├── VHALConfig.h               ← VHAL configuration
│   └── IRQ/
│       ├── SystemIrq.h / .cpp     ← system interrupts
│       └── DeviceIrq.h / .cpp     ← peripheral interrupts
├── Application/
│   └── ...                        ← application logic
├── STM32XXXX_FLASH.ld             ← linker script (from donor)
├── CMakeLists.txt
├── CMakePresets.json
└── arm-none-eabi.cmake            ← toolchain file
```

### VHAL

Download or clone [VHAL](https://github.com/VeyDlin/VHAL) into the project folder. VHAL already contains all necessary drivers (LL, CMSIS) and FreeRTOS as submodules — everything will be downloaded automatically:

```bash
git clone --recursive https://github.com/VeyDlin/VHAL.git
```

### Helper Files

These files are the same for all STM32 projects, simply copy them from `.demo/STM32G0/`:

- **`arm-none-eabi.cmake`** — toolchain file for cross-compilation. Universal for all ARM Cortex-M
- **`CMakePresets.json`** — build configurations (Debug, Release, etc.)
- **`.vscode/`** — VS Code settings: `settings.json` and `tasks.json` (Build/Clean/Configure buttons in the status bar)

Available build presets:

| Preset | Optimization | Description |
|--------|-------------|----------|
| Debug | `-O0 -g3` | No optimization, full debugging |
| Release | `-Os -g0` | Size optimization |
| RelSpeed | `-O2 -g0` | Speed optimization |
| RelDebug | `-O2 -g` | Optimization + debug information |


## Project Configuration

Open the donor project (the `.cube/` folder), we will copy configuration from it. Throughout this text, the word **(donor)** means the file should be looked up in the donor project.

### VHALConfig.h

Open `BSP/VHALConfig.h` and configure the defines for your project. Example for STM32G071 with UART, IWDG and FreeRTOS:

```cpp
#define VHAL_RUNTIME

#define VHAL_STM32
#define VHAL_STM32_G0
#define VHAL_STM32_G0_UART
#define VHAL_STM32_G0_IWDG

#define VHAL_RTOS
#define VHAL_RTOS_FREERTOS

#define VHAL_SYSTEM_CONSOLE
```

**System:**

| Define | Description |
|--------|-------------|
| `VHAL_RUNTIME` | Built-in `syscalls` and `sysmem`. Without it, provide your own `_write`, `_read`, `_sbrk` |
| `VHAL_SYSTEM_CONSOLE` | `System::console` — printf-like output via `System::SetWriteHandler()` |

**Platform** (one required):

| Define | Description |
|--------|-------------|
| `VHAL_STM32` | STM32 platform |
| `VHAL_ENS` | ENS platform |
| `VHAL_ESP32` | ESP32 platform |

**STM32 series** (one required if `VHAL_STM32`):

| Define | Description |
|--------|-------------|
| `VHAL_STM32_F4` | STM32F4 series |
| `VHAL_STM32_G0` | STM32G0 series |
| `VHAL_STM32_G4` | STM32G4 series |

**Peripherals** (add only what you use):

| STM32F4 | STM32G0 | STM32G4 | Adapter |
|---------|---------|---------|---------|
| `VHAL_STM32_F4_UART` | `VHAL_STM32_G0_UART` | `VHAL_STM32_G4_UART` | AUART |
| `VHAL_STM32_F4_SPI` | — | — | ASPI |
| `VHAL_STM32_F4_I2C` | `VHAL_STM32_G0_I2C` | — | AI2C |
| `VHAL_STM32_F4_TIM` | `VHAL_STM32_G0_TIM` | `VHAL_STM32_G4_TIM` | ATIM |
| `VHAL_STM32_F4_ADC` | `VHAL_STM32_G0_ADC` | `VHAL_STM32_G4_ADC` | AADC |
| `VHAL_STM32_F4_DAC` | `VHAL_STM32_G0_DAC` | `VHAL_STM32_G4_DAC` | ADAC |
| `VHAL_STM32_F4_DMA` | `VHAL_STM32_G0_DMA` | — | ADMA |
| `VHAL_STM32_F4_IWDG` | `VHAL_STM32_G0_IWDG` | `VHAL_STM32_G4_IWDG` | AIWDG |
| `VHAL_STM32_F4_FLASH` | — | — | AFLASH |
| — | — | `VHAL_STM32_G4_COMP` | ACOMP |

GPIO (`AGPIO`) is always available — no define needed.

**RTOS:**

| Define | Description |
|--------|-------------|
| `VHAL_RTOS` | Enable RTOS support |
| `VHAL_RTOS_FREERTOS` | FreeRTOS backend |
| `VHAL_RTOS_TIMER` | Timer classes (`TimerStatic`, `Timer`) |
| `VHAL_RTOS_CRITICAL_SECTION` | `CriticalSection` RAII wrapper |
| `VHAL_RTOS_EVENT` | Event groups |
| `VHAL_RTOS_MAILBOX` | MailBox (queue wrapper) |
| `VHAL_RTOS_MUTEX` | Mutex |

### Linker Script

Open the donor project and find the file with the `.ld` extension — it is located in the donor root or in the `cmake/` subfolder. The name looks something like: `STM32G071XX_FLASH.ld` (depends on your chip).

Copy this file to the **root** of your project.

Open the copied file and check the `MEMORY` section — make sure the FLASH and RAM sizes match your chip.

### FreeRTOSConfig.h (if using RTOS)

Open (donor) `Core/Inc/FreeRTOSConfig.h` and copy it to `Core/Inc/` of your project. The file contains settings specific to your chip (frequency, priorities, stack size).

### main.cpp

Create a `main.cpp` file in the project root and paste:

```cpp
#include <Application.h>

int main(void) {
    BSP::Init();
    Application::Init();
}
```

`BSP::Init()` performs all initialization: system configuration, clock setup, SysTick, VHAL initialization, peripheral binding, and GPIO configuration.

### System Initialization (BSP.cpp)

Open (donor) `Core/Src/main.c` and find the function `int main(void)`.

**InitSystem** — before the `SystemClock_Config()` call, some STM32 versions have an "MCU Configuration" section with code like:

```cpp
LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);
LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);
NVIC_SetPriority(PendSV_IRQn, 3);
NVIC_SetPriority(SysTick_IRQn, 3);
```

If this code exists — copy it into `BSP/BSP.cpp` → `BSP::InitSystem()`. If there is no such code — skip this step.

**InitClock** — find the function `void SystemClock_Config(void)` in the same file. Copy **all its contents** into `BSP/BSP.cpp` → `BSP::InitClock()`.

> CubeMX uses `LL_Init1msTick` to configure the SysTick interrupt frequency, but we already use `BSP::InitSystemTick`, so the `LL_Init1msTick` line can be commented out or placed in `BSP::InitSystemTick` — at your discretion.

### Interrupt Handlers

Open (donor) `Core/Src/stm32XXxx_it.c` (the name depends on the STM32 series, e.g., `stm32g0xx_it.c`).

**System interrupts** — find the handlers `NMI_Handler`, `HardFault_Handler`, `SysTick_Handler` and copy them into `BSP/IRQ/SystemIrq.cpp`. Function prototypes go in `BSP/IRQ/SystemIrq.h`.

If you use FreeRTOS, you also need to add `SVC_Handler` and `PendSV_Handler` with RTOS calls:

```cpp
void SVC_Handler(void) {
    OS::RTOS::HandleSvcInterrupt();
}

void PendSV_Handler(void) {
    OS::RTOS::HandlePendSvInterrupt();
}

void SysTick_Handler(void) {
    System::TickHandler();
    OS::RTOS::HandleSysTickInterrupt();
}
```

If you use a timer instead of SysTick, add `System::TickHandler()` and `OS::RTOS::HandleSysTickInterrupt()` to the corresponding timer handler.

**Peripheral interrupts** — handlers for your peripherals (UART, ADC, etc.) will be added later when configuring specific peripherals.


## CMakeLists.txt

Create `CMakeLists.txt` in the project root — copy from `.demo/STM32G0/CMakeLists.txt` and adapt for your chip. The file contains comments indicating where to find each parameter in the donor project.

Everything else (LL drivers, startup, system, FreeRTOS, CMSIS includes, linker) — `vhal.cmake` will include automatically.

> Without FreeRTOS — simply do not specify `VHAL_FREERTOS_PORT`.

### VHAL_CPU by Cores

| Core | VHAL_CPU | FPU flags (if needed) |
|------|----------|----------------------|
| Cortex-M0 | `cortex-m0` | — |
| Cortex-M0+ | `cortex-m0plus` | — |
| Cortex-M3 | `cortex-m3` | — |
| Cortex-M4 (with FPU) | `cortex-m4` | `-mfpu=fpv4-sp-d16 -mfloat-abi=hard` |
| Cortex-M7 (single) | `cortex-m7` | `-mfpu=fpv5-sp-d16 -mfloat-abi=hard` |
| Cortex-M7 (double) | `cortex-m7` | `-mfpu=fpv5-d16 -mfloat-abi=hard` |

### VHAL_FREERTOS_PORT by Cores

| Core | Port |
|------|------|
| Cortex-M0/M0+ | `ARM_CM0` |
| Cortex-M3 | `ARM_CM3` |
| Cortex-M4 (with FPU) | `ARM_CM4F` |
| Cortex-M7 | `ARM_CM7/r0p1` |
| Cortex-M33 | `ARM_CM33_NTZ/non_secure` |

### VHAL_FREERTOS_HEAP

| Strategy | Description |
|-----------|----------|
| `heap_1` | Allocation only, no freeing. Minimal size |
| `heap_2` | Allocation and freeing, no block merging |
| `heap_3` | Wrapper over standard `malloc`/`free` |
| `heap_4` | Allocation, freeing, merging. **Recommended** |
| `heap_5` | Like heap_4, but works with multiple memory regions |


## Building

At this stage, the project should already compile and run.

```bash
# Configuration
cmake --preset Debug

# Build
cmake --build --preset Debug

# Clean
cmake --build --preset Debug --target clean
```

Or use the buttons in the VS Code status bar (Build, Clean, Configure).

> **Configure** (`cmake --preset Debug`) — generates the build system (creates `build/Debug/`, `compile_commands.json`, `build.ninja`). Required when first opening the project or after modifying `CMakeLists.txt`. Usually runs automatically — `settings.json` has `"cmake.configureOnOpen": true`.

Switching presets: CMake Tools → status bar → select preset.


## Debugging and Flashing

For debugging and flashing, the **Cortex-Debug** extension is used. It supports ST-Link, J-Link, and other probes, and also displays peripheral registers via SVD files.

### SVD File

The SVD file describes all peripheral registers of your chip. Without it, debugging will work, but the register inspector will not.

Download the SVD for your chip:
1. Open your chip's page on [st.com](https://www.st.com) (e.g., STM32G071RB)
2. Section **Design Resources** → **SVD** → download the archive
3. Extract the `.svd` file to the project root

### launch.json

The debugger configuration is located in `.vscode/launch.json`. The `.vscode/` folder is hidden by default in VS Code — open the file through the system file explorer or remove `.vscode` from `files.exclude` in `settings.json`.

Create `.vscode/launch.json` (or copy from `.demo/STM32G0/.vscode/launch.json`):

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug (OpenOCD)",
            "type": "cortex-debug",
            "request": "launch",
            "cwd": "${workspaceFolder}",
            "executable": "${workspaceFolder}/build/Debug/MyProject.elf",
            "servertype": "openocd",
            "configFiles": [
                "interface/stlink.cfg",
                "target/stm32g0x.cfg"
            ],
            "svdFile": "${workspaceFolder}/STM32G071.svd",
            "runToEntryPoint": "main",
            "showDevDebugOutput": "none",
            "preLaunchTask": "Build"
        }
    ]
}
```

What you need to replace for your project:

| Field | Description | Where to find |
|------|----------|-----------|
| `executable` | Path to the `.elf` file | `build/<preset>/<PROJECT_NAME>.elf` |
| `servertype` | GDB server | `openocd` for ST-Link, `jlink` for J-Link |
| `configFiles` | OpenOCD configs | Depend on the probe and chip (see table) |
| `device` | Chip name (J-Link only) | E.g., `STM32G071RB` |
| `svdFile` | Path to the SVD file | Download from [st.com](https://www.st.com) |
| `preLaunchTask` | Build task before launch | Must match the `label` in `tasks.json` |

### OpenOCD configFiles by Probes

| Probe | interface | target (examples) |
|---------|-----------|-----------------|
| ST-Link | `interface/stlink.cfg` | `target/stm32g0x.cfg` |
| J-Link | `interface/jlink.cfg` | `target/stm32g0x.cfg` |
| CMSIS-DAP | `interface/cmsis-dap.cfg` | `target/stm32g0x.cfg` |

> Target files depend on the series: `stm32g0x.cfg`, `stm32f4x.cfg`, `stm32g4x.cfg`, `stm32h7x.cfg`, etc. Full list is in the OpenOCD installation folder: `share/openocd/scripts/target/`.

### J-Link Configuration

If using J-Link, replace the section with:

```json
{
    "name": "Debug (J-Link)",
    "type": "cortex-debug",
    "request": "launch",
    "cwd": "${workspaceFolder}",
    "executable": "${workspaceFolder}/build/Debug/MyProject.elf",
    "servertype": "jlink",
    "device": "STM32G071RB",
    "interface": "swd",
    "svdFile": "${workspaceFolder}/STM32G071.svd",
    "runToEntryPoint": "main",
    "showDevDebugOutput": "none",
    "preLaunchTask": "Build"
}
```

### Starting a Debug Session

**Method 1: Run and Debug panel**
1. Open the **Run and Debug** panel (`Ctrl+Shift+D`)
2. Select a configuration from the dropdown (e.g., "Debug (OpenOCD)")
3. Click ▶ (or `F5`)

**Method 2: keyboard shortcuts**
- `F5` — start debugging (uses the last selected configuration)
- `Shift+F5` — stop debugging
- `F10` — step over
- `F11` — step into
- `Shift+F11` — step out
- `F9` — toggle breakpoint

Cortex-Debug will automatically build the project (if `preLaunchTask` is specified), flash the chip, and stop at `main()`.

During debugging, the side panel will show:
- **CORTEX PERIPHERALS** — peripheral register inspector (from SVD)
- **CORTEX REGISTERS** — core registers (R0–R15, xPSR, etc.)

### Flashing Without Debugging

There is a **Flash** button in the status bar — it builds the project and flashes the chip via OpenOCD without starting the debugger. Convenient for quick firmware uploads.

The command in `.vscode/tasks.json` looks like this:

```
openocd -f interface/stlink.cfg -f target/stm32g0x.cfg -c "program build/Debug/MyProject.elf verify" -c "reset" -c "exit"
```

> **Important:** if you changed `PROJECT_NAME` in `CMakeLists.txt`, update the `.elf` filename in the Flash tasks (`.vscode/tasks.json`) and in `launch.json`. The filename always matches `PROJECT_NAME`.

**Hard reset vs Soft reset** — hard reset (hardware reset via the debugger's SRST pin) is used by default. If your debugger does not support SRST or you need a software reset, replace `-c "reset"` with `-c "soft_reset_halt"` and `-c "resume"`:

```
openocd ... -c "program ... verify" -c "soft_reset_halt" -c "resume" -c "exit"
```

| Type | Command | Description |
|-----|---------|----------|
| Hard reset | `-c "reset"` | Hardware reset via the SRST pin. Full chip restart |
| Soft reset | `-c "soft_reset_halt"` + `-c "resume"` | Software reset via AIRCR.SYSRESETREQ. Does not require the SRST pin |

> If using J-Link instead of ST-Link, replace the command in `.vscode/tasks.json` → "Flash" task:
> ```
> JLinkExe -device STM32G071RB -if swd -speed 4000 -autoconnect 1 -CommandFile flash.jlink
> ```
> And create a `flash.jlink` file:
> ```
> loadfile build/Debug/MyProject.elf
> r
> go
> exit
> ```


## BSP Peripheral Configuration

Once the project compiles, you can start adding peripherals. We will use UART as an example — if you did not enable it in CubeMX, this is a good opportunity to practice.

If you decide to add an entirely new peripheral:
- Enable it with interrupts in the donor project and regenerate the code in CubeMX (do not forget to switch HAL to LL in **Project Manager** → **Advanced Settings**)

### Adding GPIO

Adding regular GPIO is very straightforward.

Open `BSP/BSP.h` and create a `public` variable:

```cpp
class BSP {
...
public:
    ...
    static AGPIO ledPin;
```

Open `BSP/BSP.cpp` and declare it:

```cpp
AGPIO BSP::ledPin = { GPIOC, 6 };
```

In this case, it is GPIO on port C, pin 6. If you need inverted logic (low level when active), declare it like this:

```cpp
AGPIO BSP::ledPin = { GPIOC, 6, true };
```

The third parameter is inverted logic, `false` by default.

### Adding a Peripheral (UART Example)

**Step 1: Declaration** — open `BSP/BSP.h` and create a `public` variable:

```cpp
class BSP {
...
public:
    ...
    static AUART consoleSerial;
```

Open `BSP/BSP.cpp` and declare it:

```cpp
AUART BSP::consoleSerial = { USART1 };
```

In this case, `USART1` is used; yours may differ.

**Step 2: Find the configuration in the donor** — open (donor) `Core/Src/main.c` and find the initialization function for your UART, e.g., `void MX_USART1_UART_Init()`. From it, we need three things:

1. **Enabling peripheral clocking** — find a line like:
   ```cpp
   LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);
   ```
   Note: enabling GPIO clocking is not needed — VHAL will do it automatically.

2. **Interrupt configuration** (skip if not using interrupts) — find the "interrupt Init" section:
   ```cpp
   /* USART1 interrupt Init */
   NVIC_SetPriority(USART1_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 0, 0));
   NVIC_EnableIRQ(USART1_IRQn);
   ```

3. **GPIO configuration** — find the "GPIO Configuration" section and note the pins and their Alternate Mode:
   ```cpp
   /**USART1 GPIO Configuration
       PA9   ------> USART1_TX
       PA10  ------> USART1_RX
   */
   GPIO_InitStruct.Alternate = LL_GPIO_AF_1;
   ```
   In this case, it is `PA9` and `PA10`, both using Alternate Mode 1 (`LL_GPIO_AF_1`).

**Step 3: Add to BSP** — open `BSP/BSP.cpp` and in the `BSP::InitAdapterPeripheryEvents()` function, add `beforePeripheryInit` for your peripheral:

```cpp
consoleSerial.beforePeripheryInit = []() {
    // Enable clocking (from donor, step 2.1)
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);

    // GPIO in Alternate Mode (from donor, step 2.3)
    // For VHAL we use the convenient AGPIO::AlternateInit interface
    AGPIO::AlternateInit({ GPIOA, 9,  1, AGPIO::Pull::Up, AGPIO::Speed::VeryHigh });
    AGPIO::AlternateInit({ GPIOA, 10, 1, AGPIO::Pull::Up, AGPIO::Speed::VeryHigh });

    // Interrupts (from donor, step 2.2)
    NVIC_SetPriority(USART1_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 0, 0));
    NVIC_EnableIRQ(USART1_IRQn);

    return Status::ok;
};
```

`EnableClock`, `SetPriority`, `EnableIRQ` — all of these were found in the donor. For GPIO, VHAL provides a convenient `AGPIO::AlternateInit` interface — no need to fill in `GPIO_InitStruct` manually.

> The peripheral configuration itself (UART baud rate, ADC resolution, etc.) is handled by VHAL — you only need to set up clocks, GPIO, and interrupts.

**Step 4: Interrupt handler** (if using interrupts) — open (donor) `Core/Src/stm32XXxx_it.c` (the name depends on the series, e.g., `stm32g0xx_it.c`) and find the interrupt handler for UART (e.g., `USART1_IRQHandler`).

Copy the function prototype into `BSP/IRQ/DeviceIrq.h`, and in `BSP/IRQ/DeviceIrq.cpp` add the handler with a VHAL call:

```cpp
void USART1_IRQHandler(void) {
    BSP::consoleSerial.IrqHandler();
}
```

### Adding Other Peripherals

For any other peripheral (I2C, SPI, ADC, Timer), the process is identical:
1. Declare the adapter in `BSP.h` / `BSP.cpp`
2. Find the `MX_xxx_Init()` function in the donor and extract EnableClock, GPIO Alternate, NVIC
3. Add `beforePeripheryInit` in `BSP::InitAdapterPeripheryEvents()`
4. Add the interrupt handler in `BSP/IRQ/DeviceIrq`


## Adding a New STM32 Series

If VHAL does not yet contain submodules for your series (check the `Periphery/Adapter/Port/STM32/` folder), add them:

```bash
cd VHAL/

# LL/HAL drivers
git submodule add https://github.com/STMicroelectronics/stm32{x}xx-hal-driver.git \
    Periphery/Adapter/Port/STM32/{X}/Drivers/HAL

# CMSIS Device
git submodule add https://github.com/STMicroelectronics/cmsis-device-{x}.git \
    Periphery/Adapter/Port/STM32/{X}/Drivers/CMSIS/Device

# CMSIS Core (if not already present in another series)
git submodule add https://github.com/STMicroelectronics/cmsis_core.git \
    Periphery/Adapter/Port/STM32/{X}/Drivers/CMSIS/Core
```

Where `{x}` is the series in lowercase (f4, g0, h7), `{X}` is in uppercase (F4, G0, H7).

All repositories: [STMicroelectronics on GitHub](https://github.com/orgs/STMicroelectronics/repositories).

> **Note**: CMSIS Device repository names may differ (`cmsis_device_g0` vs `cmsis-device-f4`). Verify the exact name on GitHub.


## Checklist

- [ ] VHAL cloned with `--recursive`
- [ ] `VHAL_RUNTIME` enabled in `VHALConfig.h`
- [ ] Linker script copied from donor and verified (correct chip, memory sizes)
- [ ] `FreeRTOSConfig.h` copied from donor (if FreeRTOS)
- [ ] Clocks transferred from donor `SystemClock_Config()` to `BSP::InitClock()`
- [ ] System init transferred to `BSP::InitSystem()` (if present in donor)
- [ ] Defines transferred from donor (`STM32XXXxx`, `USE_FULL_LL_DRIVER`, `HSE_VALUE`, ...)
- [ ] `CMakeLists.txt` filled in (CPU flags, series, .ld name, startup name, defines)
- [ ] `SysTick_Handler` contains `System::TickHandler()` and `OS::RTOS::HandleSysTickInterrupt()`
- [ ] `beforePeripheryInit` filled in for each peripheral (EnableClock, GPIO, NVIC)
- [ ] Interrupt handlers added in `BSP/IRQ/`
- [ ] Project builds without errors
