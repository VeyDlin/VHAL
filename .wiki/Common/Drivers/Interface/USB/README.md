# USB Device Stack

A full-speed USB device stack for VHAL. It is a device only: it answers a host and never acts as one. It is written in the VHAL style. It uses no heap, no RTOS and no global state.

The library has no identity. It has no vendor or product ID, no strings, no GUID and no host-specific code. The application gives all of them through its descriptor set. The test tools use their own test identities, kept in `.tool`.

Headers:

- `#include <Drivers/Interface/USB/Device/USBDevice.h>` for the device core
- `#include <Drivers/Interface/USB/Device/USBFunction.h>` for the function base class
- `#include <Drivers/Interface/USB/Descriptor/...>` for the descriptor builders
- `#include <Drivers/Interface/USB/Class/Vendor/VendorBulkFunction.h>` for the vendor bulk function

Status: the core, the descriptor builders, WebUSB, the built-in classes (vendor bulk, CDC-ACM, CDC-NCM, HID, MIDI, USB Audio 1.0, mass storage and DFU), the STM32G4 port, the host simulator tests, the G431 test firmware (`.demo/STM32G4`) and the PC benchmark `usbbench` (`.tool/usbbench`) are in the tree. The device side runs on an STM32G431 with Windows and Linux as hosts; see [Host Support](#host-support) and [the measured results](#measured-results).

## Scope

| In | Out |
|----|-----|
| Full-speed device on the STM32G4 `fsdev` controller, see [USBDeviceAdapterG4](../../../../Periphery/Adapter/Port/STM32/G4/USBDeviceAdapterG4.md) | Host role |
| Control pipe, standard requests, SET_ADDRESS, one configuration with interface alternate settings | High speed. All current ports are full speed, with no external PHY |
| Class and vendor requests, routed to the function that owns the interface or endpoint | USB Audio 2.0, and a speaker with a feedback endpoint (the USB Audio 1.0 speaker is adaptive, see [Class/Audio](Class/Audio/README.md)) |
| USB Audio 1.0: a microphone and a speaker on isochronous endpoints, with mute, volume and sampling rates, see [Class/Audio](Class/Audio/README.md) | - |
| Descriptor builders that run at compile time and check their input | USB Power Delivery, Link Power Management, battery charging detection |
| Built-in classes, each for one job: vendor bulk (a program's own protocol, in buffered and streaming modes), CDC-ACM (a serial port), CDC-NCM (a network adapter, Ethernet over USB with NTB-16), HID (keyboard, mouse and raw reports), MIDI 1.0 (music messages), mass storage (a disk, Bulk-Only with SCSI) and DFU 1.1 (firmware update, run-time and mode) | - |
| Optional Microsoft OS 2.0 component, which lets Windows bind WinUSB without an INF file, and the optional WebUSB landing page | Any identity or host-specific default |

Isochronous endpoints are in the adapter and the G4 port: one packet per frame, with no handshake and no halt. On fsdev the DTOG bit selects the packet buffer; it is not a USB data toggle. They are the base of USB Audio 1.0, see [Class/Audio](Class/Audio/README.md). The contract is in [USBDeviceAdapter](../../../../Periphery/Adapter/USBDeviceAdapter.md#isochronous-endpoints).

Also planned: the STM32G0 `fsdev` port (G0B1 and G0C1 only) and the STM32F4 OTG port (`dwc2`). The G0 and F4 folders in `Periphery/Adapter/Port/STM32/` have no USB adapter yet.

CDC-NCM, the network adapter, carries Ethernet frames only (see [Class/CDC/NcmFunction](Class/CDC/NcmFunction.md)). VHAL has no TCP/IP stack for STM32 yet, so [SocketAdapter](../../../../Periphery/Adapter/SocketAdapter.md) and [HttpClient](../Network/Http/README.md) do not run over it. The USBNetwork demo answers DHCP, ARP and ping with its own small responder, in the `Network` folder of `.demo/STM32G4/Application/DemoUSBNetwork`.

### Host-agnostic

A device built with this stack is written to work with any standard host; only the hosts in [Host Support](#host-support) are tested. The core and the classes make no assumption about the host operating system. Only the optional MS OS 2.0 component is for Windows. Other hosts ignore it. A vendor-class device should need no driver on hosts with a generic USB API (libusb, IOKit, the Android USB host API).

## Host Support

| Host | Hardware test | Simulator |
|------|---------------|-----------|
| Windows 11 | Verified on an STM32G431: WinUSB bound through the MS OS 2.0 component with no INF, `usbbench` IN, OUT, loopback, both directions and reconnect on the WinUSB and libusb backends (see [Measured Results](#measured-results)) | The Windows enumeration order, `TestEnumeration.cpp` |
| Linux: Ubuntu 22.04 in WSL2 (kernel 6.18) through usbipd-win 5.3.0 on the same PC | Verified on an STM32G431: enumeration and every descriptor read by `lsusb -v`, `usbbench` IN, OUT, loopback, both directions and reconnect with the system libusb 1.0.25 (see [Measured Results](#measured-results)) | The Linux enumeration order, `TestEnumeration.cpp` |
| macOS | Not available. Untested on hardware | The macOS enumeration order, `TestEnumeration.cpp`. Nothing more |
| Android | Not available. Untested on hardware | No simulator order exists |

## Layers

| Layer | Source | Page |
|-------|--------|------|
| Controller interface | `Periphery/Adapter/USBDeviceAdapter.h` | [USBDeviceAdapter](../../../../Periphery/Adapter/USBDeviceAdapter.md) |
| Shared types (endpoint type, speed, setup bytes, endpoint addresses) | `Periphery/Adapter/Utilities/USBTypes.h` | [USBDeviceAdapter](../../../../Periphery/Adapter/USBDeviceAdapter.md#shared-types) |
| STM32G4 controller port (`fsdev`) | `Periphery/Adapter/Port/STM32/Common/USBFsdev.h`, `Periphery/Adapter/Port/STM32/G4/USBDeviceAdapterG4.h` | [USBDeviceAdapterG4](../../../../Periphery/Adapter/Port/STM32/G4/USBDeviceAdapterG4.md) |
| Chapter 9 values and setup packet | `Common/Drivers/Interface/USB/USBProtocol.h` | [Device](Device/README.md) |
| Device core and function base | `Common/Drivers/Interface/USB/Device/USBDevice.h`, `Common/Drivers/Interface/USB/Device/USBFunction.h` | [Device](Device/README.md) |
| Descriptor builders: fluent (`Descriptor/Builder/`) and positional (`Descriptor/*.h`) | `Common/Drivers/Interface/USB/Descriptor/` | [Descriptor](Descriptor/README.md) |
| Microsoft OS 2.0 (optional) | `Common/Drivers/Interface/USB/Descriptor/MsOs20/*.h` | [Descriptor/MsOs20](Descriptor/MsOs20/README.md) |
| Vendor bulk function | `Common/Drivers/Interface/USB/Class/Vendor/*.h` | [Class/Vendor](Class/Vendor/README.md) |
| Bulk engine and buffered channel, shared by vendor bulk and CDC-ACM | `Common/Drivers/Interface/USB/Class/Common/*.h` | [Class/CDC](Class/CDC/README.md) and [Class/Vendor](Class/Vendor/README.md). No page of its own |
| CDC-ACM serial port | `Common/Drivers/Interface/USB/Class/CDC/*.h` | [Class/CDC](Class/CDC/README.md) |
| CDC-NCM network adapter and NTB-16 codec | `Common/Drivers/Interface/USB/Class/CDC/NcmFunction.h`, `NcmNtb16.h`, `USBCdcNcmBuilder.h` | [Class/CDC/NcmFunction](Class/CDC/NcmFunction.md) |
| HID: report descriptors, keyboard, mouse, raw reports | `Common/Drivers/Interface/USB/Class/HID/*.h` | [Class/HID](Class/HID/README.md) |
| USB-MIDI 1.0 | `Common/Drivers/Interface/USB/Class/MIDI/*.h` | [Class/MIDI](Class/MIDI/README.md) |
| USB Audio 1.0 microphone and speaker | `Common/Drivers/Interface/USB/Class/Audio/*.h` | [Class/Audio](Class/Audio/README.md) |
| Mass storage (Bulk-Only, SCSI) | `Common/Drivers/Interface/USB/Class/MSC/*.h` | [Class/MSC](Class/MSC/README.md) |
| DFU 1.1: run-time and mode functions, memory target | `Common/Drivers/Interface/USB/Class/DFU/*.h` | [Class/DFU](Class/DFU/README.md) |
| WebUSB landing page (optional) | `Common/Drivers/Interface/USB/Descriptor/WebUsb/*.h` | [Descriptor/WebUsb](Descriptor/WebUsb/README.md) |
| Bootloader transport over CDC-ACM or vendor bulk | `Common/Utilities/Bootloader/Communication/USBCommunication.h` | [Bootloader](../../../Utilities/Bootloader/README.md#usbcommunication----usb-transport) |
| Host tests, test firmware, `usbbench` and test identities | `Common/Drivers/Interface/USB/.tool/` (the pages write it as `.tool/`) | [Tests and tools](#tests-and-tools) |

The controller is a separate class from the core, so the core does not depend on a vendor handle. The register code for `fsdev` lives once in `Port/STM32/Common/`. The family file only gives its constants.

## Build a Device

The steps below build a device with one vendor bulk function. Each step names the object it creates. The order matters: the clock must be on before the device starts. The clock, interrupt and device steps follow the bench firmware in `.demo/STM32G4`, and the BSP code of step 1 is that firmware's code. The descriptors and the function are the example's own: the host tests do not compile them. The bench firmware's descriptors are in `.demo/STM32G4/Application/DemoUSBBench/Descriptors/BenchDescriptors.h`.

### 1. Clock and interrupt in the BSP

The STM32G4 BSP must give the USB peripheral a 48 MHz clock, enable its bus clock and set the interrupt. The adapter does not do this itself. It checks that the USB clock is on, and returns `notReady` if it is not. This is `BSP::InitUsbClock()` from `.demo/STM32G4/BSP/BSP.cpp`:

```cpp
void BSP::InitUsbClock() {
	// HSI48 is the 48 MHz USB clock. CRS trims it against the USB start-of-frame (1 kHz), so the clock needs no crystal and
	// stays within the USB tolerance. The reload value is 48 MHz / 1 kHz - 1.
	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_CRS);

	LL_RCC_HSI48_Enable();
	while (LL_RCC_HSI48_IsReady() != 1) {}

	LL_CRS_ConfigSynchronization(
		LL_CRS_HSI48CALIBRATION_DEFAULT,
		LL_CRS_ERRORLIMIT_DEFAULT,
		47999U,
		LL_CRS_SYNC_SOURCE_USB | LL_CRS_SYNC_POLARITY_RISING | LL_CRS_SYNC_DIV_1
	);
	LL_CRS_EnableFreqErrorCounter();
	LL_CRS_EnableAutoTrimming();

	LL_RCC_SetUSBClockSource(LL_RCC_USB_CLKSOURCE_HSI48);
	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USB);

	// Only USB_LP serves the controller. USB_HP stays disabled in the NVIC: the adapter's flags would be served from two
	// vectors otherwise.
	NVIC_SetPriority(USB_LP_IRQn, 1);
	NVIC_EnableIRQ(USB_LP_IRQn);
}
```

Call `InitUsbClock()` from `BSP::Init()` after the clock is set up, and before `device.Start()`. The PA11 (USB D-) and PA12 (USB D+) pins also need the USB alternate function, with no pull. The bench firmware sets both to alternate function 10:

```cpp
AGPIO::AlternateInit({ GPIOA, 11, 10, AGPIO::Pull::None, AGPIO::Speed::VeryHigh }); // USB D-
AGPIO::AlternateInit({ GPIOA, 12, 10, AGPIO::Pull::None, AGPIO::Speed::VeryHigh }); // USB D+
```

Take the number from the device datasheet for another board. The repository headers do not define it.

### 2. Adapter and interrupt

The adapter is a plain object. Its constructor takes the peripheral handle, which defaults to `USB`. The BSP owns one instance, and the vector function forwards to it. This is the bench firmware's `BSP/IRQ/DeviceIrq.cpp`:

```cpp
extern "C" void USB_LP_IRQHandler(void) {
	BSP::usbAdapter.IrqHandler();
}
```

`AUSB` is the alias of `USBDeviceAdapterG4` on the G4 port. Declare it in the BSP's header: `static AUSB usbAdapter;`. Step 5 must use this same instance.

### 3. Descriptors

The descriptors are written with the fluent builder. The chain runs at compile time, the checks are compile errors, and the arrays it produces are constexpr values in static storage. The application gives every identity value: VID, PID, strings, GUID. The result is a class with the `USBDescriptorSet` view as `AppUsb::Set`:

```cpp
using AppUsb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(VendorId)
		.ProductId(ProductId)
		.DeviceRelease(0x0100)
		.Manufacturer("Example")
		.Product("Vendor Bulk Device")
		.SerialNumberAtRuntime()
		.MsOs20VendorCode(0x20)
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBVendorBuilder()
				.Name("Vendor bulk")
				.BulkIn(0x81)
				.BulkOut(0x02)
				.WinUsb(InterfaceGuid)));
}>;
```

`VendorId`, `ProductId` and `InterfaceGuid` are the product's own values; the builder has no defaults for them. The full chain, with its rules, is on the [Descriptor](Descriptor/README.md) page. The Microsoft OS 2.0 part is on the [Descriptor/MsOs20](Descriptor/MsOs20/README.md) page, and the descriptor bytes are in the Descriptor page's positional example.

### 4. Functions

A function owns the interfaces and endpoints it was given. Its numbers must match the descriptors. `AppUsb::Function(index)` gives the layout of a function in the chain, so the function and the descriptors come from the same source. The vendor bulk function owns one interface and two endpoints:

```cpp
VendorBulkFunction<4096, 4096> vendor(AppUsb::Function(0));
```

The first template argument is the TX ring size in bytes. The second is the RX ring size in bytes. The RX ring must hold one whole OUT transfer.

### 5. Device

The device core takes the adapter, the descriptor set and the list of functions. It keeps the spans, not copies. The objects must outlive the device. The adapter is the BSP's instance from step 2: the interrupt drives that one, so the device must use it too. A second adapter would never see the bus events.

```cpp
USBFunction* functions[] = { &vendor };
USBDevice device(BSP::usbAdapter, AppUsb::Set, functions);
```

Put these at file scope, or in `main()` where they live until the program ends.

### 6. Start, Process and Stop

`Start()` checks the descriptor set and the functions. It gives the adapter the highest endpoint number of the configuration (`SetHighestEndpointNumber()`), applies the adapter parameters, subscribes to the events and connects the pull-up. After it returns `ok`, the host can see the device. It returns `invalidParameter` when the adapter refuses that highest number. On a port with a buffer table sized from it, that is an endpoint number of 8 to 15 named by any interface or alternate setting, which the port could never open. `Process()` runs the deferred work of each function. Call it from the main loop. The set here reserves the serial number (`SerialNumberAtRuntime()`), so the string is given with `SetSerialNumber()` before the first `Start()`; see [Serial Number](Device/README.md#serial-number):

```cpp
int main() {
	BSP::Init();

	// Thread context, before the first Start(). The helper builds the string from the MCU's unique ID; it is shown on the Descriptor
	// page under Runtime Serial Number. Without the string, Start() refuses and the check below reports it.
	SetUniqueSerialNumber();

	ResultStatus status = device.Start(USBDeviceAdapter::Parameters());
	if (status != ResultStatus::ok) {
		System::CriticalError("USB start failed");
	}

	while (true) {
		device.Process();
	}
}
```

`Stop()` detaches the device. It changes endpoint state, so the caller wraps it in a critical section:

```cpp
System::CriticalSection(true);
device.Stop();
System::CriticalSection(false);
```

## Rules at a Glance

| Context | Calls |
|---------|-------|
| Interrupt | Adapter events, `USBFunction` callbacks other than `Process()`, the vendor bulk completion, the streaming producer and consumer |
| Thread | `Start()`, `Stop()`, `Process()`, `Write()`, `Read()`, `SetProducer()`, `SetConsumer()` |
| Either | `ControlSendData()`, `StartInTransfer()`, `StartOutTransfer()`, `Stall()`, `ClearStall()`: see the [controller contract](../../../../Periphery/Adapter/USBDeviceAdapter.md) |

- `System::CriticalSection` nests on every port, so a stack call that opens its own section may run inside the application's. Keep such sections short. `Stop()` must be wrapped by the caller.
- The core never blocks, never allocates and never enters a critical section by itself.
- Anything slow is deferred to `Process()`, which runs in the main loop or a thread.

## Limits

| Item | Limit |
|------|-------|
| Speed | Full speed only |
| Configurations | One. `bNumConfigurations` must be 1 |
| Endpoint numbers | `Parameters::endpointCount` (default 4). At most 8 on the G4 port, endpoint 0 included |
| Packet memory (G4) | 1024 bytes, shared by all endpoints. Endpoint 0 takes 128 bytes, and the buffer table 8 bytes for each endpoint number up to the highest in the descriptors (64 bytes when none is set) |
| Bulk packet size | 8, 16, 32 or 64 bytes. The vendor function uses 64 |
| Interrupt packet size | 1 to 64 bytes |
| Isochronous packet size | 1 to 1023 bytes, one packet per frame, `bInterval` 1. On the G4 two packet buffers of the rounded size per endpoint (see the [G4 port](../../../../Periphery/Adapter/Port/STM32/G4/USBDeviceAdapterG4.md#packet-memory-cost)), so only a small one fits beside the others |
| Control packet size | 64 bytes (`bMaxPacketSize0`) |
| Device address | 0 to 127 |
| String descriptor | 126 UTF-16 code units |
| Configuration | `wTotalLength` up to 65535 bytes. `bMaxPower` up to 250 (500 mA) |
| Vendor bulk transfer | `PacketsPerTransfer` packets (default 8, that is 512 bytes) |

## Tests and Tools

### Host tests: `.test`

The simulator is a software controller that follows the adapter contract. A scripted host drives the core through it. The tests run on a PC. They need no board.

Run them from the repository root:

```sh
./test.cmd USB
```

- Needs CMake 3.25 or newer and a g++ with C++23. The tests were run with g++ 15.2. Set `CXX` to use another compiler.
- Works from cmd, PowerShell and Git Bash on Windows, and from a shell on Linux and macOS.
- Each `Test*.cpp` is built with `-std=c++23 -Wall -Wextra -Werror` and run. The runner exits non-zero when a test does not build or a check fails.
- Build output goes to `.test-build`, which is git-ignored.

`./test.cmd USB -L host` builds and runs the 60 test files (`Test*.cpp`), and `./test.cmd USB -L fail` runs the 92 builder error snippets in `.test/BuilderErrors/`. Each snippet is a compile-error check with its expected message. The MS OS 2.0 check is covered on its [page](Descriptor/MsOs20/README.md).

The tests cover:

- the controller against the adapter contract, without the core
- the Windows, Linux and macOS enumeration orders, strings, BOS and the MS OS 2.0 request
- a SETUP that interrupts a data stage, and the zero-length packet rule
- stalls, their clearing, and endpoint halts
- SET_CONFIGURATION, GET_CONFIGURATION, SET_INTERFACE and GET_INTERFACE
- suspend, resume and remote wakeup
- routing of class, vendor and bulk traffic to the owning function
- the vendor bulk FIFOs at their limits
- the classes, each on its own page: HID, CDC-ACM, CDC-NCM, MIDI, USB Audio 1.0, mass storage and DFU, with the bootloader's USB transport over CDC-ACM and vendor bulk
- run-time strings: the string table the application fills after it starts, the refusal of an empty or over-long descriptor, the device's refusal to start before every string is given, and the hex-string descriptor's length limit
- the network demo (`DemoUSBNetwork`): the Internet checksum, and the ARP, ICMP echo, DHCP and UDP responders with their malformed and foreign frames

### Benchmark firmware and `usbbench`

The G431 firmware is in `.demo/STM32G4` (the STM32G4 demo, which is the USB benchmark for now) and [`usbbench`](.tool/usbbench/README.md) is in `.tool/usbbench`. Their protocol is in `.tool/common/BenchProtocol.h`, which both sides include. The header holds the test identities. They are for tests only. A product must use its own.

- Endpoints: bulk IN `0x81`, bulk OUT `0x02`, 64-byte packets, interface 0. `0x02` and not `0x01`, so both endpoints get double buffering on fsdev (see [Class/Vendor](Class/Vendor/README.md))
- Vendor requests to the interface: `bmRequestType` `0x41` (OUT) or `0xC1` (IN). The codes are `SetMode` (`0x01`), `GetCounters` (`0x02`, 32 bytes), `ResetCounters` (`0x03`) and `Reconnect` (`0x04`)
- Modes: Idle, Source, Sink, SourceAndSink and Loopback
- The data pattern is an xorshift32 stream from seed `0x12345678`. The receiver checks it byte by byte

A run: build the firmware, flash it with the ST programmer, wait for the device, run `usbbench` in IN, OUT or loopback mode, and compare the rate with the threshold.

Several boards on one PC: each board reports its MCU unique ID as its USB serial number. `usbbench list` shows every attached board with its serial number and where it sits on the bus. `--serial <text>` picks one board for any command. With several boards attached and no `--serial`, a command stops and lists them instead of guessing. `--all` runs `in`, `out`, `loopback` or `sourcesink` on every board at once, starting their transfers together, and prints one line per board and a total. The total is the aggregate rate of the bus per direction: the bytes of all boards over the time from the first transfer start to the last transfer end. Its exit code is non-zero if any board failed. `info`, `counters` and `reconnect` act on one board only.

### Class check: `usbclasscheck`

`usbclasscheck` in `.tool/usbclasscheck` checks the USB classes of the STM32G4 demos on a real board, end to end: the composite's serial port, keyboard, mouse and vendor channel, the storage demo's disk, the DFU demo's download and upload, the network demo's adapter, DHCP lease, echoes and ARP entry, and the audio demo's microphone tone and speaker loopback (the endpoints are found in Windows audio by their USB device, and a 440 Hz test tone plays into the board's speaker for a second; no default device or volume is changed). It prints PASS or FAIL for each step, and it exits non-zero on a failure. The download overwrites the DFU region, the keyboard check moves the pointer briefly, and the audio check is audible on the board's speaker. Its [page](.tool/usbclasscheck/README.md) has the build, the usage and what each check changes.

### Measured Results

STM32G431 at 170 MHz (USB clock from HSI48 trimmed by CRS), full speed, bulk endpoints double-buffered, `.demo/STM32G4` with 2 KB transfers. Host: Windows 11, Intel xHCI, the board on a root port. `usbbench` with 8 transfers of 64 KB in flight.

| Mode | WinUSB | libusb | Data errors |
|------|--------|--------|-------------|
| IN, 1 GiB | 1.152 MB/s | 1.152 MB/s (32 MiB run) | 0 |
| OUT, 1 GiB | 1.025 MB/s | 1.024 MB/s (32 MiB run) | 0 |
| IN and OUT at once | 0.559 + 0.559 MB/s | the same | 0 |
| Loopback | 0.489 MB/s each way | the same | 0 |
| Reconnect, pull-up off 500 ms | back after 680 to 750 ms | about 700 ms | |

Linux, the same board and port, forwarded to Ubuntu 22.04 in WSL2 by usbipd-win, `usbbench` with the system libusb:

| Mode | Rate | Data errors |
|------|------|-------------|
| IN, 64 MiB | 1.152 MB/s | 0 |
| OUT, 64 MiB | 1.031 MB/s | 0 |
| IN and OUT at once, 32 MiB | 0.567 + 0.567 MB/s | 0 |
| Loopback, 16 MiB | 0.490 MB/s each way | 0 |
| Reconnect, pull-up off 500 ms | back after 4.3 s with `usbipd attach --auto-attach` | |

The rates match Windows because the bus, not the host, sets them. The reconnect time is mostly usbipd attaching the device to WSL again. Without `--auto-attach`, forwarding ends when the device disconnects, which is how usbip works.

Full speed carries at most 19 bulk packets of 64 bytes per 1 ms frame, about 1.2 MB/s, so IN is close to the bus limit. The rates depend on the path to the host:

- Behind a USB 2.0 hub with a single transaction translator (the board was first on a Genesys GL850G), every full-speed transfer goes through split transactions and the same board gave about 0.8 MB/s. Use a root port, or a multi-TT hub when several full-speed devices must run at full rate at once.
- On fsdev a double-buffered IN endpoint is at most one packet ahead of the CPU: the controller sends nothing while DTOG_TX equals SW_BUF, which was checked on the board with the CPU halted. Every IN packet therefore needs the interrupt to release the next buffer, and the port releases it with the first register write of the interrupt.

## Tools

The three host tools have a page each. Each tool folder has a short README that points to its page.

| Tool | Page | What it does |
|------|------|--------------|
| test | [.test](.test/README.md) | Host tests of the device stack on a software controller. No board |
| usbbench | [.tool/usbbench](.tool/usbbench/README.md) | Throughput and data check of the benchmark firmware, on libusb or WinUSB |
| usbclasscheck | [.tool/usbclasscheck](.tool/usbclasscheck/README.md) | End-to-end class check of the demo boards. Runs on a real board and changes some state on the PC and the board |
| overview | [.tool](.tool/README.md) | The three tools, their folders and the test identities they share |

## Pages

- [USBDeviceAdapter](../../../../Periphery/Adapter/USBDeviceAdapter.md): the controller contract
- [USBDeviceAdapterG4](../../../../Periphery/Adapter/Port/STM32/G4/USBDeviceAdapterG4.md): the STM32G4 port
- [Device](Device/README.md): the device core, states, control stages and functions
- [Descriptor](Descriptor/README.md): the descriptor builders, fluent (`Descriptor/Builder/`) and positional
- [Descriptor/MsOs20](Descriptor/MsOs20/README.md): the Microsoft OS 2.0 component
- [Descriptor/WebUsb](Descriptor/WebUsb/README.md): the WebUSB landing page, an optional BOS capability
- [Class/Vendor](Class/Vendor/README.md): the vendor bulk function
- [Class/CDC](Class/CDC/README.md): the CDC class, with the CDC-ACM serial port
- [Class/CDC/NcmFunction](Class/CDC/NcmFunction.md): the CDC-NCM network adapter
- [Class/HID](Class/HID/README.md): the HID class: report descriptors, the function, keyboard and mouse
- [Class/MIDI](Class/MIDI/README.md): the USB-MIDI 1.0 function
- [Class/Audio](Class/Audio/README.md): the USB Audio 1.0 microphone and speaker function, on isochronous endpoints
- [Class/MSC](Class/MSC/README.md): the mass storage class, Bulk-Only Transport and SCSI
- [Class/DFU](Class/DFU/README.md): the DFU 1.1 run-time and mode functions, with the memory target
