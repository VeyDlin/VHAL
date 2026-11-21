# usbclasscheck

Checks the USB classes of the STM32G4 demo boards (`.demo/STM32G4`) end to end, on a real board. It prints PASS or FAIL for each step, and it exits non-zero when a check fails.

Documentation: [`.wiki/Common/Drivers/Interface/USB/.tool/usbclasscheck/README.md`](../../../../../../.wiki/Common/Drivers/Interface/USB/.tool/usbclasscheck/README.md)
(also on the documentation site under Drivers, Interface, USB, `.tool`, `usbclasscheck`).

## Build

Run from the `.tool` folder (Windows, MinGW-w64 g++ 15, CMake, Ninja):

```
cmake -S usbclasscheck -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build
ctest --test-dir build
```

The tests are the pure logic only. They need no board.

## Quick start

```
usbclasscheck list               # the attached demo boards
usbclasscheck network            # the adapter, the DHCP lease, echoes and ARP
```

Read the wiki page before a run. Some checks change state. The `dfu` check overwrites the DFU region of the board. The `composite` check moves the pointer and sends the F13 to F24 keys. The `audio` check plays a tone through the board's speaker. Before it, set the default playback and recording devices back to the PC's own devices (Settings, Sound), because Windows makes a new audio device the default.
