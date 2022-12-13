# VHAL
VHAL - C++20 HAL library for STM32.

WHAL takes over the configuration of the periphery, all that is needed is to declare in BSP the necessary GPIOs and the periphery that will be used at the application level.

It should also be understood that VHAL does not allow the application to change the pinout (alternative pins) they are fixed in BSP.

BSP requires manual installation at the LL (registers) level:
- System
  - System configuration (relevant for older versions, things like clocking the power domain)
  - Kernel clock configuration
  - SysTick configuration and adding to interrupt handlers for RTOS and `RTOS::HandleSysTickInterrupt()` and `System::TickHandler()`

- Peripherals (Such as I2C, UART, TIMER... except GPIO)
  - Initialization of `EnableClock` for each iteration
  - Initializing GPIO in Alternate Mode
  - Initialization of `NVIC_SetPriority` and `NVIC_EnableIRQ` if interrupts are used
  
# Creating a project in CubeIDE and CubeMX

As an example the following is taken `STM32F412RETx`

## Initial setup
- Create a new project in CubeMX and configure standard things such as clocking and enabling SW debugging (SYS ->Debug -> Serial Wire)
-  Use Sytick as Timebase Sourse (SYS -> Timebase Sourse). You can also use the timer as you wish, but make sure it initializes first because some VHAL adapters use `System::TickHandler()` for initialization (ADC can use a delay to have time to calibrate correctly)
- Enable FreeRTOS in the `Mindware` tab, the CMSYS version does not matter, we only need the FreeRTOS file and for the project to be generated taking into account that RTOS will be used. This step can also be skipped, but many drivers and utilities use RTOSAdapter, be careful.

## Setting up the periphery
- Activate the peripherals in CUbeMX you need and their interrupts (if you are going to use interrupts). Nothing else is needed, the rest can be configured directly from VHAL.

## Project generation
- Go to the `Project Manager` tab -> and select `Toolchain / IDE` -> `STM32CubeIDE`
- In the `Project Manager` tab -> `Advanced Settings`, select each peripheral and change `HAL` to `LL` (since VHAL uses LL)
- Click the `Generate Code` button (if CubeMX asks if you are sure you want to use SyTick - click `yes`)

# Create VHAL Project
- Duplicate the newly generated project, it will be used as a code donor
- Open the original project

## Config IDE
- Donvload or clone [VHAL](https://github.com/VeyDlin/VHAL) and copy `VHAL` to project root
- Copy `Application` and `BSP` to project root
- `Right mouse button` -> `Refresh` (or F5)
- `Right mouse button` -> `Convert to C++`
- `Right mouse button` -> `Properties` 
  - `C/C++ Build`
    - (advice) tab `Behavior` -> `Enable Parallel Build` and `Use unlimited jobs`
    - (advice) `Settings` -> `MCU GCC Compiler` -> `Geniral` -> `Standart` -> GNU18 (Update the cube if you don't have GNU18)
    - `Settings` -> `MCU G++ Compiler` -> `Geniral` -> `Standart` -> GNU++20 (Update the cube if you don't have GNU++20)
  - `C/C++ Geniral` -> `Path and Symbols`
    - `Includes` -> ... (`Add to all configurations` and `Add to all languages`)
      - `Add` -> `Directory`: `Application`
      - `Add` -> `Directory`: `BSP`
      - `Add` -> `Directory`: `VHAL/Common`
      - `Add` -> `Directory`: `VHAL/Periphery`
    - `Source Location`
      - `Add Folder...` -> `Application`
      - `Add Folder...` -> `BSP`
      - `Add Folder...` -> `VHAL`

## Project preparation
- Delete all files from the `Core` folder except `FreeRTOSConfig.h`, `main.c`, `freertos.c`, `system_stm32XXX.c`
- Rename `main.c` to `main.cpp`. It should look something like this:
  - `Core`
    - `Inc`
      - FreeRTOSConfig.h
    - `Src`
      - freertos.c
      - main.cpp
      - system_stm32f4xx.c (The name differs depending on the version of the STM32)
  
 - Delete everything from `main.cpp` and paste this code

   ```c++
    #include <Application.h>
   
    int main(void) {
      BSP::Init();
      Application::Init();
    }
   ```

## Config VHAL Project base
Open a donor project to copy configurations from there. In the future, if you need to add a new peripheral or modify an existing one, use CubeMX to regenerate the donor project and copy the configurations according to the instructions below.

- Open the file (donor) `Core/Inc/main.h` and copy all include and PRIORITYGROUP (if any) to `BSP/Periphery.h` ([example](https://github.com/VeyDlin/VHAL_Template/blob/main/BSP/Periphery.h))
- Adapters
  -Open `BSP/PortAdapters.h` and add include adapters of the used peripherals for your STM32 series in the format `#include <Adapter/Port/F_X_/_PERIPHERY_AdapterF_X_.h>` ([example](https://github.com/VeyDlin/VHAL_Template/blob/main/BSP/PortAdapters.h))
    - Please note that you will have a compilation error if you add adapters to the peripherals that you did not select in CubeMX because CubeMX generates LL library files only for the peripherals that are activated.
- Interrupts (this step is optional and only for familiarization since the necessary code has already been written in the template)
  - Open the file (donor) `Core/Inc/stm32f4xx_it.h` (the name may differ from the STM32 series) and copy functions prototypes to `BSP/IRQ/SystemIrq.h` ([example](https://github.com/VeyDlin/VHAL_Template/blob/main/BSP/IRQ/SystemIrq.h))
  - Open the file (donor) `Core/Src/stm32f4xx_it.c` (the name may differ from the STM32 series) and copy Exception Handlers to `BSP/IRQ/SystemIrq.cpp` ([example](https://github.com/VeyDlin/VHAL_Template/blob/main/BSP/IRQ/SystemIrq.cpp))
   Add handlers for `RTOS` and `System` to `SysTick_Handler` ([example](https://github.com/VeyDlin/VHAL_Template/blob/main/BSP/IRQ/SystemIrq.cpp#L45)):
  
    ```c++
    System::TickHandler();
    OSAdapter::RTOS::HandleSysTickInterrupt();
    ```
   If you use a timer instead of a SysTick, you should add the code there.
- System
  -  Open the file (donor) `Core/Src/main.c`
    - In the function `int main(void)` before calling `SystemClock_Config()`, there is an MCU Configuration in some versions of STM32, add the code from there to `BSP/BSP.cpp` -> `BSP::InitSystem()`([example](https://github.com/VeyDlin/VHAL_Template/blob/main/BSP/BSP.cpp#L11)). If you don't have this code, then skip this step.
    - In the `void SystemClock_Config(void)` function copy all the contents to `BSP/BSP.cpp` -> `BSP::InitClock()` ([example](https://github.com/VeyDlin/VHAL_Template/blob/main/BSP/BSP.cpp#L30))
      - CubeMX uses [LL_Init1msTick](https://github.com/VeyDlin/VHAL_Template/blob/main/BSP/BSP.cpp#L56) to configure the interrupt frequency `SysTick_Handler`, but we already use [BSP::InitSystemTick](https://github.com/VeyDlin/VHAL_Template/blob/main/BSP/BSP.cpp#L65), so the line with `LL_Init1msTick` can be commented out, or inserted into `BSP::InitSystemTick` at your discretion.

At this stage, the project should already be successfully compiled and running.

## Config VHAL Project periphery
For example, we will use UART, if you have not included it in the project, then this is a great opportunity to practice.

In case you decide to add a completely new periphery:
-  Enable UART with interrupts in the donor project and then run the CubeMX generator. (don't forget to change `HAL` to `LL` in the `Project Manager` tab -> `Advenced Settings`)
- Copy the new libraries that are in the folder `Drivers/STM32F4xx_HAL_Driver` (the name may differ from the STM32 series)
  - (The easiest way is to simply copy the entire `Drivers` folder from the donor project to the original project with a replacement.
- Add a new include of your periphery to `BSP/PortAdapters.h`

### Adding GPIO

Adding regular GPIOs is extremely simple.
- Open `BSP/BSP.h` and create a `public` variable `static AGPIO` ([example](https://github.com/VeyDlin/VHAL_Template/blob/main/BSP/BSP.h#L10))

  ```c++
  class BSP {
  ...
  public:
    ...
    static AGPIO ledPin;
  ```
- Open `BSP/BSP.cpp` and declare your variable ([example](https://github.com/VeyDlin/VHAL_Template/blob/main/BSP/BSP.cpp#L6))

  ```c++
  AGPIO BSP::ledPin = { GPIOC, 6 };
  ```
  In this case, it is GPIO6. If you want its signal to be low when set (inverse logic), then you can declare it like this:
  
  ```c++
  AGPIO BSP::ledPin = { GPIOC, 6, true };
  ```
  3 optional parameter specifies whether inverse logic is used, by default it is `false`
  
### Adding another periphery

For example UART is used.
- Open `BSP/BSP.h` and create a `public` variable `static AUART` ([example](https://github.com/VeyDlin/VHAL_Template/blob/main/BSP/BSP.h#L8))

  ```c++
  class BSP {
  ...
  public:
    ...
    static AUART consoleSerial;
  ```
- Open it `BSP/BSP.cpp ` and declare your variable ([example](https://github.com/VeyDlin/VHAL_Template/blob/main/BSP/BSP.cpp#L4))

  ```c++
  AUART BSP::consoleSerial = { USART1 };
  ```
  In this case, `USART1` is used, but you may have another one.
- Open the file (donor) `Core/Src/main.c` and find your UART initialization function, for example `void MX_USART1_UART_Init()`
  You only need the function of enabling the UART clock and GPIO on which it will run.
  - Find in `void MX_USART1_UART_Init()` peripheral clock enable, in this case it is:
  
    ```c++
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART2);
    ```
  Note that you don't need the peripheral clock enable for GPIO, it will be enabled automatically by VHAL.
  - Find in `void MX_USART1_UART_Init()` interrupt Init, in this case it is:
    (you can skip this step if you don't use interrupts)
    
    ```c++
    /* USART1 interrupt Init */
    NVIC_SetPriority(USART1_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 0, 0));
    NVIC_EnableIRQ(USART1_IRQn);
    ```
  - Find the UART GPIO Configuration and remember GPIOs and their Alternate Mode
    ```c++
    /** USART1 GPIO Configuration
        PA7   ------> USART1_TX
        PA10   ------> USART1_RX
    */
    GPIO_InitStruct.Pin = LL_GPIO_PIN_2 | LL_GPIO_PIN_3;
    GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
    GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
    GPIO_InitStruct.Alternate = LL_GPIO_AF_7;
    LL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    ```
    In this case it's `PA7` and `PA10`, both use 7 Alternative Mode (`GPIO_Init Struct.Alternative = LL_GPIO_AF_7;`)
- Open it `BSP/BSP.cpp` and add to `BSP::InitAdapterPeripheryEvents()` the `beforePeripheryInit` event for your perm ([example](https://github.com/VeyDlin/VHAL_Template/blob/main/BSP/BSP.cpp#L76)):

  ```c++
	consoleSerial.beforePeripheryInit = []() {
		LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);
  
		AGPIO::AlternateInit<AGPIO>({ GPIOA, 9,  7, AGPIO::Pull::Up, AGPIO::Speed::VeryHigh });
		AGPIO::AlternateInit<AGPIO>({ GPIOA, 10, 7, AGPIO::Pull::Up, AGPIO::Speed::VeryHigh });
  
		NVIC_SetPriority(USART1_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 0, 0));
		NVIC_EnableIRQ(USART1_IRQn);
  
		return Status::ok;
	};
  ```
  `EnableClock`, `SetPriority`, `EnableIRQ` copied from a donor project, there is a convenient interface for GPIO `AGPIO::AlternateInit`
- And finally, if you use interrupts, add an interrupt handler to BSP
  - Open the file (donor) `Core/Inc/stm32f4xx_it.h` (the name may differ from the STM32 series) and copy functions prototypes to `BSP/IRQ/DeviceIrq.h` ([example](https://github.com/VeyDlin/VHAL_Template/blob/main/BSP/IRQ/DeviceIrq.h#L7))
  - Open the file (donor) `Core/Src/stm32f4xx_it.c` (the name may differ from the STM32 series) and copy Exception Handlers to `BSP/IRQ/DeviceIrq.cpp` ([example](https://github.com/VeyDlin/VHAL_Template/blob/main/BSP/IRQ/DeviceIrq.cpp#L9))
  
  Then add a VHAL handler for your UART:
  
  ```c++
  BSP::consoleSerial.IrqHandler();
  ```
  
# Done
You have created a project and configured BSP, now you can use your peripherals, you can read about how to configure it at the top level in the documentation for [VHAL](https://github.com/VeyDlin/VHAL)
