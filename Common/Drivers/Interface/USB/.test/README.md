# USB device host tests (simulator)

Host tests for the USB device stack on the PC. A software controller stands in for the hardware, and a scripted host drives the device core through it. The tests need no board.

Documentation: [`.wiki/Common/Drivers/Interface/USB/.test/README.md`](../../../../../../.wiki/Common/Drivers/Interface/USB/.test/README.md)
(also on the documentation site under Drivers, Interface, USB, `.test`).

## Run

From the repository root, with CMake 3.25 or newer and a g++ that supports C++23 (tested with g++ 15.2):

```
./test.cmd USB
```

The runner builds each `Test*.cpp` and runs it. It exits non-zero when a test does not build or a check fails. Set `CXX` to use another compiler. It works from cmd, PowerShell, Git Bash and a shell on Linux and macOS.
