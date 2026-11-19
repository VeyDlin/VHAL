# USB Device Stack

A full-speed USB device stack for VHAL. It is a device only: it answers a host and never acts as one. It is written in the VHAL style. It uses no heap, no RTOS and no global state.

The library has no identity. It has no vendor or product ID, no strings, no GUID and no host-specific code. The application gives all of them through its descriptor set. The test tools use their own test identities, kept in `.tool`.

Headers:

- `#include <Drivers/Interface/USB/Device/USBDevice.h>` for the device core
- `#include <Drivers/Interface/USB/Device/USBFunction.h>` for the function base class
- `#include <Drivers/Interface/USB/Descriptor/...>` for the descriptor builders
- `#include <Drivers/Interface/USB/Class/Vendor/VendorBulkFunction.h>` for the vendor bulk function

Status: the core, the descriptor builders, the vendor bulk function, the STM32G4 port, the host simulator tests, the G431 test firmware (`.demo/STM32G4`) and the PC benchmark `usbbench` (`.tool/usbbench`) are in the tree. The device side runs on an STM32G431 with Windows as the host; see [Host Support](#host-support) and [the measured results](#measured-results).

## Scope

| In | Out |
|----|-----|
| Full-speed device on the STM32G4 `fsdev` controller, see [USBDeviceAdapterG4](../../../../Periphery/Adapter/Port/STM32/G4/USBDeviceAdapterG4.md) | Host role |
| Control pipe, standard requests, SET_ADDRESS, one configuration with interface alternate settings | High speed. All current ports are full speed, with no external PHY |
| Class and vendor requests, routed to the function that owns the interface or endpoint | Isochronous transfers |
| Descriptor builders that run at compile time and check their input | USB Power Delivery, Link Power Management, battery charging detection |
| Vendor bulk function, in a buffered mode and a streaming mode | Built-in classes. CDC, HID, MSC and DFU come later |
| Optional Microsoft OS 2.0 component, which lets Windows bind WinUSB without an INF file | Any identity or host-specific default |

Planned, in this order: CDC-ACM, HID, MSC, DFU (a transport for the VHAL bootloader), and composite devices. After that come the STM32G0 `fsdev` port (G0B1 and G0C1 only) and the STM32F4 OTG port (`dwc2`).

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

`VendorId`, `ProductId` and `InterfaceGuid` are the product's own values; the builder has no defaults for them. The full chain, with its rules, is on the [Descriptor](Descriptor/README.md) page. The Microsoft OS 2.0 part is on the [Descriptor/MsOs20](MsOs20/README.md) page, and the descriptor bytes are in the Descriptor page's positional example.

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

`Start()` checks the descriptor set and the functions. It applies the adapter parameters, subscribes to the events and connects the pull-up. After it returns `ok`, the host can see the device. `Process()` runs the deferred work of each function. Call it from the main loop. The set here reserves the serial number (`SerialNumberAtRuntime()`), so the string is given with `SetSerialNumber()` before the first `Start()`; see [Serial Number](Device/README.md#serial-number):

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
| Packet memory (G4) | 1024 bytes, shared by all endpoints. Endpoint 0 takes 128 bytes |
| Bulk packet size | 8, 16, 32 or 64 bytes. The vendor function uses 64 |
| Interrupt packet size | 1 to 64 bytes |
| Control packet size | 64 bytes (`bMaxPacketSize0`) |
| Device address | 0 to 127 |
| String descriptor | 126 UTF-16 code units |
| Configuration | `wTotalLength` up to 65535 bytes. `bMaxPower` up to 250 (500 mA) |
| Vendor bulk transfer | `PacketsPerTransfer` packets (default 8, that is 512 bytes) |

## Tests and Tools

### Host tests: `.tool/sim`

The simulator is a software controller that follows the adapter contract. A scripted host drives the core through it. The tests run on a PC. They need no board.

Run them from the `.tool/sim` folder:

```sh
sh run.sh
```

- Needs a g++ with C++20. The tests were run with g++ 15.2. Set `CXX` to use another compiler.
- Works in Git Bash on Windows, and on Linux and macOS.
- Each `Test*.cpp` is built with `-std=c++20 -Wall -Wextra -Werror` and run. The script exits non-zero when a test does not build or a check fails.
- Build output goes to a temporary folder outside the repository.

Status: all 15 test executables pass, with the builder error snippets. The MS OS 2.0 check is covered on its [page](Descriptor/MsOs20/README.md).

The tests cover:

- the controller against the adapter contract, without the core
- the Windows, Linux and macOS enumeration orders, strings, BOS and the MS OS 2.0 request
- a SETUP that interrupts a data stage, and the zero-length packet rule
- stalls, their clearing, and endpoint halts
- SET_CONFIGURATION, GET_CONFIGURATION, SET_INTERFACE and GET_INTERFACE
- suspend, resume and remote wakeup
- routing of class, vendor and bulk traffic to the owning function
- the vendor bulk FIFOs at their limits

### Benchmark firmware and `usbbench`

The G431 firmware is in `.demo/STM32G4` (the STM32G4 demo, which is the USB benchmark for now) and `usbbench` is in `.tool/usbbench`. Their protocol is in `.tool/common/BenchProtocol.h`, which both sides include. The header holds the test identities. They are for tests only. A product must use its own.

- Endpoints: bulk IN `0x81`, bulk OUT `0x02`, 64-byte packets, interface 0. `0x02` and not `0x01`, so both endpoints get double buffering on fsdev (see [Class/Vendor](Class/Vendor/README.md))
- Vendor requests to the interface: `bmRequestType` `0x41` (OUT) or `0xC1` (IN). The codes are `SetMode` (`0x01`), `GetCounters` (`0x02`, 32 bytes), `ResetCounters` (`0x03`) and `Reconnect` (`0x04`)
- Modes: Idle, Source, Sink, SourceAndSink and Loopback
- The data pattern is an xorshift32 stream from seed `0x12345678`. The receiver checks it byte by byte

A run: build the firmware, flash it with the ST programmer, wait for the device, run `usbbench` in IN, OUT or loopback mode, and compare the rate with the threshold.

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

## Pages

- [USBDeviceAdapter](../../../../Periphery/Adapter/USBDeviceAdapter.md): the controller contract
- [USBDeviceAdapterG4](../../../../Periphery/Adapter/Port/STM32/G4/USBDeviceAdapterG4.md): the STM32G4 port
- [Device](Device/README.md): the device core, states, control stages and functions
- [Descriptor](Descriptor/README.md): the descriptor builders
- [Descriptor/MsOs20](Descriptor/MsOs20/README.md): the Microsoft OS 2.0 component
- [Class/Vendor](Class/Vendor/README.md): the vendor bulk function
