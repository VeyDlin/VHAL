# USB Host Tools

The host tools for the USB stack run on a PC. One of them tests the device without a board. The other two use the STM32G4 demo boards. Each tool has a page of its own, and its source folder has a short README that links to that page.

- [test](../.test/README.md): the host tests of the device stack. A software controller follows the adapter contract, and a scripted host drives the core through it. It needs no board, and it runs with `./test.cmd USB`.
- [usbbench](usbbench/README.md): the throughput and data-check benchmark for the USB benchmark firmware (`.demo/STM32G4`). It runs on libusb, or on native WinUSB on Windows, and it needs a board.
- [usbclasscheck](usbclasscheck/README.md): the end-to-end check of the demo classes (composite, storage, DFU, network and audio) on a real board. It prints PASS or FAIL for each step. Some steps change the PC or the board, so read the page before a run.

The source folder is `Common/Drivers/Interface/USB/.tool/`. Two files are shared by the tools: `common/BenchProtocol.h`, the bench protocol that both the firmware and `usbbench` include, and `common/DemoProducts.h`, the test identities of the demo boards. Both hold test values only. A product must use its own identities.

The USB stack's own page, with the host support and the measured results, is [USB Device Stack](../README.md).
