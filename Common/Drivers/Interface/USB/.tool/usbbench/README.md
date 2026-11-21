# usbbench

Host-side throughput and data-check benchmark for the USB benchmark firmware in `.demo/STM32G4`. It reports MB/s, data errors and the device counters after each run. It runs on libusb (Windows, Linux and macOS), or on native WinUSB on Windows.

Documentation: [`.wiki/Common/Drivers/Interface/USB/.tool/usbbench/README.md`](../../../../../../.wiki/Common/Drivers/Interface/USB/.tool/usbbench/README.md)
(also on the documentation site under Drivers, Interface, USB, `.tool`, `usbbench`).

## Build

Run from the `.tool` folder (Windows, MinGW-w64 g++ 15, CMake, Ninja):

```
cmake -S usbbench -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build
```

On Windows the configure step downloads libusb 1.0.30 into `third_party/`. Linux and macOS use the system libusb-1.0 (`libusb-1.0` must be installed).

## Quick start

The board needs the benchmark firmware from `.demo/STM32G4`.

```
usbbench list                    # the attached boards and their serial numbers
usbbench in 64m                  # device sends, host checks
usbbench loopback 16m            # device returns the host data, host checks
```

With more than one board attached, pick one with `--serial <text>`, as `list` prints it, or run `--all` for `in`, `out`, `loopback` or `sourcesink` on every board.
