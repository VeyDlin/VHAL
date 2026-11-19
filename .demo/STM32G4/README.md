# STM32G4 demo (STM32G431)

A demo project for the STM32G4 family on VHAL, next to `.demo/STM32G0`. For now it is the USB benchmark device: the
firmware for the USB device stack (`Common/Drivers/Interface/USB`). It enumerates as the test device (pid.codes
`1209:0001`, WinUSB through the Microsoft OS 2.0 descriptors) with one vendor interface and two bulk endpoints, and
answers the vendor requests of `Common/Drivers/Interface/USB/.tool/common/BenchProtocol.h`. That header is shared with
the PC benchmark `usbbench`, so the demo includes it from the tool folder (an include directory in `CMakeLists.txt`).
Later the demo will show other G4 peripherals as well.

The descriptors are checked at compile time against `BenchProtocol.h` (static asserts in `Application/DemoUSBBench/Descriptors/BenchDescriptors.h`):
the IDs, the vendor request code and the interface and endpoint numbers must match what `usbbench` expects.

## Build

```sh
cmake --preset RelSpeed
cmake --build build/RelSpeed
```

Outputs in `build/RelSpeed/`: `USBBenchG431.elf` and `USBBenchG431.bin` (the `.bin` is written at 0x08000000).
The build needs `arm-none-eabi-gcc` and Ninja on the PATH. Presets: `Debug`, `Release` (size), `RelSpeed` (speed,
use this for the benchmark), `RelDebug`.

## Flash

```sh
sh flash.sh
```

Writes the `.bin` over SWD with STM32CubeProgrammer, verifies it and resets the board. Set `STM32_PROGRAMMER_CLI` to
use another CLI path, and `STM32_SWD_SN` when more than one ST-LINK is connected.

## Behaviour

- Clock: HSI16 through the PLL, 170 MHz SYSCLK (range 1 boost, 4 flash wait states). USB 48 MHz from HSI48, trimmed by CRS
  from the USB start-of-frame. SysTick 1 kHz.
- USART1 (PA9 TX, PA10 RX), 115200 8N1. `System::console` is connected to it (`VHAL_SYSTEM_CONSOLE` in `BSP/VHALConfig.h`,
  the write handler in `BSP::Init()`). Its writes never wait: the text goes into a 512-byte ring, and the USART1 transmit
  interrupt sends it in 32-byte chunks through the adapter's asynchronous transmit (`WriteArrayAsync()`, next chunk from the
  `onInterrupt` callback). When the ring is full, characters are dropped and counted in `BSP::consoleDropped` (readable over
  SWD), so the main loop and the USB pump never block on the UART. Every second it prints one line, ending in CR LF, with the
  USB state, the bench counters, the bus resets and the adapter error count. If USART1 fails to start, no write handler is
  set and nothing is printed; `benchConsoleStatus` holds the result (readable over SWD like `benchCounters`).
- USB pins PA11/PA12 (alternate function 10). UCPD dead-battery pull-downs are disabled.
- Vendor interface modes (SetMode): Idle, Source, Sink, SourceAndSink, Loopback. ResetCounters, GetCounters, and Reconnect
  (D+ off for the requested milliseconds, then attach again; the main loop does the disconnect, not the interrupt).
- `benchCounters` (a volatile global in `Application/DemoUSBBench/DemoUSBBench.cpp`) holds the counters for reading over SWD. Find its
  address in `build/RelSpeed/USBBenchG431.map`, or read it with `STM32_Programmer_CLI -c port=SWD -r <addr> 32`.

## Layout

- `BSP/`: clocks, USART1 and the console ring (`System::console` write handler and transmit chunks), USB clock, interrupt
  vectors, `VHALConfig.h`.
- `Application/`: `Application.h/.cpp` chooses and runs the demo; nothing else is in it.
  - `Application/DemoUSBBench/`: the USB benchmark device. `DemoUSBBench.h/.cpp` has the USB device start, the main loop
    with the USB pump, the Reconnect handling and the once-a-second status line. Its two domains sit in their own folders:
    `Descriptors/BenchDescriptors.h` has the descriptors, `Function/BenchFunction.h` the bench function.
  - Later demos (for example a HID composite device) go in their own folder beside `DemoUSBBench/`.
- `STM32G431XB_FLASH.ld`: linker script (128 KB flash, 22 KB RAM: SRAM1 16 KB plus SRAM2 6 KB, contiguous; the 10 KB CCM
  SRAM is not used).
- `arm-none-eabi.cmake`, `CMakePresets.json`, `CMakeLists.txt`: build on `vhal.cmake`. `CMakeLists.txt` finds VHAL two
  levels up (`.demo/STM32G4/../..`) and the shared `BenchProtocol.h` in `Common/Drivers/Interface/USB/.tool/common`.
