# USB device host tests (simulator)

Host tests for the USB device stack on the PC. A software controller stands in for the hardware, and a scripted host drives the device core through it. The tests need no board.

Documentation: [`.wiki/Common/Drivers/Interface/USB/.tool/sim/README.md`](../../../../../../.wiki/Common/Drivers/Interface/USB/.tool/sim/README.md)
(also on the documentation site under Drivers, Interface, USB, `.tool`, `sim`).

## Run

From this folder, with a g++ that supports C++23 (tested with g++ 15.2):

```
sh run.sh
```

The script builds each `Test*.cpp` and runs it. It exits non-zero when a test does not build or a check fails. Set `CXX` to use another compiler. It works in Git Bash on Windows, and on Linux and macOS.
