# DFU 1.1 (Device Firmware Upgrade)

The DFU class of the USB device stack, version 1.1. Two parts: the run-time function, which lets a host ask a running application to restart into its loader, and the DFU mode function, which runs the download and upload state machine over a memory target. Both are control-pipe only: a DFU interface has no endpoints.

Headers:

- `#include <Drivers/Interface/USB/Class/DFU/USBDfuBuilder.h>` for the class builder (`.Runtime()` or `.Mode()`)
- `#include <Drivers/Interface/USB/Class/DFU/DfuRuntimeFunction.h>` for the run-time function
- `#include <Drivers/Interface/USB/Class/DFU/DfuFunction.h>` for the mode function
- `#include <Drivers/Interface/USB/Class/DFU/IDfuTarget.h>` for the memory interface the mode function writes to
- `#include <Drivers/Interface/USB/Class/DFU/DfuFlashTarget.h>` for an example target over the generic FLASH adapter
- `#include <Drivers/Interface/USB/Class/DFU/USBDfuProtocol.h>` for the request, state and status values

Status: the descriptors, the run-time function, the mode state machine, the memory regions as alternate settings and the flash example target are implemented and tested on the PC (see [Tests](#tests)). The flash example target serves one region only (see [Open](#open)).

## Build the interface

A run-time device and a mode device, both in one configuration:

```cpp
using AppUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0003)
		.DeviceRelease(0x0100)
		.Manufacturer("VHAL")
		.Product("Application with DFU")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBDfuBuilder()
				.Runtime()
				.WillDetach()
				.TransferSize(64)
				.WinUsb("{5A1D1C2E-7C3B-4D2A-9B1E-0F6A2C3D4E5F}")))
		.MsOs20VendorCode(0x20);
}>;
```

The loader that the application restarts into uses the same builder with `.Mode()`, a different product ID, and the attributes it supports:

```cpp
using LoaderUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		/* the same identity fields, with a different ProductId() */
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBDfuBuilder()
				.Mode()
				.CanDnload()
				.CanUpload()
				.ManifestationTolerant()
				.DetachTimeoutMs(1000)
				.TransferSize(64)
				.Name("Flash")
				.WinUsb("{5A1D1C2E-7C3B-4D2A-9B1E-0F6A2C3D4E5F}")))
		.MsOs20VendorCode(0x20);
}>;
```

Use a different product ID for the run-time and the mode device. Windows caches the descriptors of a VID/PID, so the two modes need distinct IDs to bind WinUSB and to be told apart.

### Builder options

| Call | Descriptor effect | Rule |
|------|-------------------|------|
| `.Runtime()` / `.Mode()` | protocol 0x01 / 0x02 | exactly one of them, once |
| `.CanDnload()`, `.CanUpload()` | bmAttributes bits 0 and 1 | a mode interface needs at least one of them |
| `.ManifestationTolerant()` | bit 2: the device answers after manifestation | off by default: the host resets the device |
| `.WillDetach()` | bit 3: the device restarts itself | run time: after DFU_DETACH; mode: after manifestation, when the device is not ManifestationTolerant |
| `.DetachTimeoutMs(n)` | wDetachTimeOut | run time: at least 1 unless `.WillDetach()`; a mode interface may leave it out |
| `.TransferSize(n)` | wTransferSize, the largest block | required, 1 to 4096 bytes |
| `.Name(text)` | iInterface | optional. One region only: not with `.Region()` |
| `.Region(name)` | one alternate setting per region, each with its name as iInterface | mode only, one to four. See [Memory regions](#memory-regions) |
| `.WinUsb()` / `.WinUsb("{GUID}")` | MS OS 2.0 WinUSB binding | needs `.MsOs20VendorCode()` on the device |

The functional descriptor is the same for both modes: bLength 9, bDescriptorType 0x21, bcdDFUVersion 0x0110. The interface is class 0xFE, subclass 0x01.

## Memory regions

A mode device with more than one memory region lists one alternate setting per region (DFU 1.1 section 4.2.3, the note on alternate settings). Each region is named by its interface string:

```cpp
.Function(USBDfuBuilder()
	.Mode()
	.CanDnload()
	.CanUpload()
	.ManifestationTolerant()
	.DetachTimeoutMs(100)
	.TransferSize(64)
	.Region("Flash")
	.Region("Config"))
```

- The first `.Region()` is alternate 0, the next one alternate 1, and so on. Up to four.
- The descriptor is one interface, `bNumInterfaces` 1, with one interface descriptor per region (they share the interface number, each with its `bAlternateSetting` and its own iInterface), then one functional descriptor after them. A device with no `.Region()` writes one interface descriptor with `.Name()` as before.
- `.Region()` is for a mode interface. It is not given together with `.Name()`.
- The host selects a region with SET_INTERFACE, and `dfu-util -a N` sends it. The mode function accepts only the alternates the descriptor lists: any other one stalls and the state stays. GET_INTERFACE answers the selection.
- SET_INTERFACE is accepted only in dfuIDLE, with no write or manifestation running. A switch in the middle of a download would send the rest of the stream to another region, so the host selects the region before the first DFU_DNLOAD and keeps it until the transfer ends (DFU_ABORT, or the end of the manifestation, returns the device to dfuIDLE).
- The transfer starts on the selected alternate: `IDfuTarget::Begin(alternate)` receives it, and the target writes that region. A target that serves one region returns errTARGET for any other alternate.
- A bus reset, a deconfiguration and a configuration return the device to alternate 0.

The run-time descriptor carries the same bmAttributes as the mode descriptor. DFU 1.1 section 4.1.3 makes the descriptor identical for both sets, and the attribute bits describe the DFU mode's capabilities. So `.CanDnload()` and `.CanUpload()` on a `.Runtime()` interface are accepted and written as the spec puts them. The run-time function uses only `.WillDetach()`.

Errors are compile errors that print the rule: `a DFU mode interface downloads or uploads`, `wTransferSize is 1 to 4096 bytes`, `TransferSize() is required`, `a device without WillDetach() needs DetachTimeoutMs() of at least 1`, `Runtime() and Mode() are given once`. The snippets in `.tool/sim/BuilderErrors/dfu_*.cpp` check each one.

## The run-time function

```cpp
DfuRuntimeFunction runtimeFunction(AppUsb::Function(0));
runtimeFunction.SetDetachDelegate(DfuRuntimeFunction::DetachDelegate::Bind<&OnDetach>());
```

- DFU_DETACH moves the state to appDETACH and starts the detach timer. The timer is wDetachTimeOut, or the host's wTimeout when that is non-zero and smaller. wTimeout 0 means wDetachTimeOut. The request is answered at once.
- The detach delegate is called from `Process()`, in thread context, once per request, with the WillDetach bit. Without WillDetach, restart the MCU into the loader: the host's USB reset follows, and the loader enumerates in DFU mode. With WillDetach, restart the MCU too: the device does not wait for a host reset. `USBDevice::Stop()` and `Start()` present the same descriptors again, so they do not change what the host sees; the restart is a reset of the MCU.
- A host reset inside the timer keeps the request: the delegate runs from the next `Process()`, even when that runs after the timer.
- Without a reset inside the timer, `Process()` returns the state to appIDLE and does not call the delegate. A reset after the timer does nothing, as the specification asks.
- DFU_GETSTATUS answers appIDLE or appDETACH with status OK and a poll timeout of 0. DFU_GETSTATE answers the state. Any other request stalls.

## The mode function

```cpp
DfuFlashTarget<Flash> target(flash, regionStart, regionSize, pageSize, key1, key2, 10);
DfuFunction<64> dfuFunction(LoaderUsb::Function(0), target);
```

The template argument is the block buffer size. It must be at least the descriptor's wTransferSize: `OnConfigured()` refuses the configuration otherwise, so the device does not enumerate with a block it cannot hold.

The state machine follows DFU 1.1 appendix A:

| Start | Request | Next state |
|-------|---------|------------|
| dfuIDLE | DFU_DNLOAD, wLength > 0 | dfuDNLOAD-SYNC (the block is received in the interrupt) |
| dfuDNLOAD-SYNC | DFU_GETSTATUS, block not yet written | dfuDNBUSY (answers the target's write poll timeout) |
| dfuDNLOAD-SYNC | DFU_GETSTATUS, block written | dfuDNLOAD-IDLE |
| dfuDNBUSY | `Process()` writes the block | dfuDNLOAD-SYNC (or dfuERROR with the target's code) |
| dfuDNLOAD-IDLE | DFU_DNLOAD, wLength > 0 | dfuDNLOAD-SYNC |
| dfuDNLOAD-IDLE | DFU_DNLOAD, wLength = 0, `IsComplete()` | dfuMANIFEST-SYNC |
| dfuMANIFEST-SYNC | DFU_GETSTATUS, first | dfuMANIFEST (answers the manifest poll timeout) |
| dfuMANIFEST | `Process()` runs `Manifest()` | dfuMANIFEST-SYNC (tolerant) or dfuMANIFEST-WAIT-RESET |
| dfuMANIFEST-SYNC | DFU_GETSTATUS, manifestation done | dfuIDLE |
| dfuMANIFEST-WAIT-RESET | bus reset | dfuIDLE; the detach delegate is called with `false`, so the application restarts into its new firmware |
| dfuIDLE | DFU_UPLOAD, wLength > 0 | dfuUPLOAD-IDLE (the block is read in the interrupt) |
| dfuUPLOAD-IDLE | DFU_UPLOAD, block shorter than wLength (short frame) | dfuIDLE (end of the upload) |
| dfuIDLE, dfuDNLOAD-IDLE, dfuUPLOAD-IDLE | DFU_ABORT | dfuIDLE, status OK |
| dfuERROR | DFU_CLRSTATUS | dfuIDLE, status OK |
| any other request the table does not allow | stall | dfuERROR, status errSTALLEDPKT |

A stall keeps the first error code: while the device is in dfuERROR, the status stays what it was when the error happened, until DFU_CLRSTATUS. A standard request to the interface (SET_FEATURE, for one) is not a DFU request: the core stalls it and the state does not change.

While `Write()` or `Manifest()` runs, DFU_DNLOAD, DFU_UPLOAD and DFU_GETSTATE stall, as appendix A.2.5 and A.2.7 say (any class request in those states stalls). DFU_GETSTATUS is the exception: it answers the busy state with the poll timeout, which is a deliberate deviation (see [Open](#open)). The target is in use until the call returns, including after a bus reset, so a second transfer cannot touch the memory or the block the running write reads.

A control answer the core cannot send (`ControlSendData()` fails) moves the device to dfuERROR with errUNKNOWN and stalls the request. A result that comes back after the device left the state it started in is dropped: a write that finishes after a reset, a deconfiguration or a stall does not change the state.

The status codes are the spec's (errTARGET to errSTALLEDPKT). A target returns the code of its failure, and the device reports it.

### Target

`IDfuTarget` is the memory the function writes:

| Method | Runs in | Contract |
|--------|---------|----------|
| `Begin(alternate)` | interrupt | Starts a transfer on the alternate the host selected with SET_INTERFACE (the region; 0 when none). errTARGET for one the target does not serve. Keep it to register writes. |
| `Write(offset, data)` | `Process()` | Writes a block at a stream offset. Returns the code of the failure. |
| `Read(offset, data)` | interrupt | Returns the number of bytes read. A short count ends an upload. |
| `Manifest()` | `Process()` | Completes the image. Returns OK when the device can continue. |
| `GetPollTimeoutMs(op)` | any | The time the host waits before the next status request, in milliseconds. |
| `IsComplete()` | interrupt | Optional, defaults to true. False answers a zero-length download with errNOTDONE. |

Offsets count from the first block of a transfer, not from the block number, because blocks can be shorter at the end.

### Example target over FLASH

`DfuFlashTarget<FlashType>` works over any type with the FLASH adapter's methods: `Unlock`, `Lock`, `PageErase`, `WriteData` and `Read`. It programs in 8-byte units, keeps a partial unit until the next block completes it, pads the last unit with 0xFF in `Manifest()`, erases each page as data reaches it (`errERASE`, and `errCHECK_ERASED` when a page does not read back blank), and reads back every unit (`errVERIFY`). The region and its page size are constructor arguments; the region size must be a multiple of 8 bytes and of the page size, and the page size must not be 0. `Begin()` returns errTARGET for any other region, before the first erase, and `ErasePage` never erases a page that ends past the region (errADDRESS).

The flash is unlocked only while `Write()` and `Manifest()` program it, and locked again when they return. An upload, an abort or an error leaves it locked.

Alternate setting 0 is the region the flash target serves. A device with one region has no other alternate, so the host only ever starts a transfer on 0. The target checks the alternate in `Begin()` and nowhere else:

- `Begin(0)` checks the region (page size, whole pages and units) and starts the transfer.
- `Begin(n)` for any other `n` returns `errTARGET`. The function stalls the DNLOAD or UPLOAD request, moves to dfuERROR with `errTARGET`, and the host reads that code with DFU_GETSTATUS.
- `Read()` does not look at the alternate, and the target's `Read()` needs no `Begin()`. The function always calls `Begin()` before `Read()`, so the same check applies to an upload.
- A bus reset, a deconfiguration and a configuration return the selection to alternate 0 (see Memory regions), so the target's next `Begin()` gets 0 unless the host selects another alternate again.

## Using dfu-util 0.11

dfu-util is installed on the development PC. The examples use the example IDs of the snippets above (`1209:0003` run time, `1209:0002` mode); replace them with the device's own.

```text
dfu-util -l
```

Lists the devices in DFU mode and their alternate settings with their names.

```text
dfu-util -d 1209:0003 -e
```

Sends DFU_DETACH to a run-time device, then resets the USB bus (dfu-util 0.11 prints "Resetting USB..." after the detach). The reset is the signal the device waits for: a device without WillDetach must restart into its loader within the time it reported in wDetachTimeOut, so the loader enumerates in DFU mode. The stack keeps the request if the reset comes inside that window, even when the application's `Process()` runs later, but a restart that comes after the window is too late, and the host finds the device gone. A device with WillDetach restarts itself and does not need the reset. `-E` sets the delay dfu-util waits before it opens the device again.

The host's wTimeout does not extend the window: the device uses the smaller of wTimeout and wDetachTimeOut, and wTimeout 0 means wDetachTimeOut.

```text
dfu-util -d 1209:0002 -a 0 -D firmware.bin
dfu-util -d 1209:0002 -a 1 -D config.bin
dfu-util -d 1209:0002 -a 0 -U backup.bin
dfu-util -d 1209:0002 -a 0 -t 64 -D firmware.bin
dfu-util -d 1209:0002 -R
```

`-a N` selects the alternate setting N with SET_INTERFACE, so a download or upload goes to region N (the `-l` listing shows the names). Downloads a file, uploads the region to a file, sets the block size (`-t`, at most the device's wTransferSize), and resets the device after manifestation (`-R`).

Notes:

- dfu-util warns when a downloaded file has no DFU suffix (DFU 1.1 appendix B). The device does not check the suffix. Add it with `dfu-suffix`, which ships with dfu-util.
- `-a N` must name a region the device lists. For an alternate the device does not have, SET_INTERFACE stalls and the device keeps its selection.
- A device with `.ManifestationTolerant()` stays on the bus after the download. A device without it stays in the loader until the host resets it: `-R` sends the reset, and the stack then calls the detach delegate, which restarts the MCU into the new firmware. A device with `.WillDetach()` and without `.ManifestationTolerant()` restarts itself after manifestation and needs no `-R`.

### Windows: WinUSB

dfu-util talks to the device through libusb, and libusb on Windows needs WinUSB bound to the interface. The builder's `.WinUsb()` (or `.WinUsb("{GUID}")`) puts the binding into the MS OS 2.0 descriptors, so Windows binds WinUSB with no INF file and dfu-util opens the device without a driver step. It needs `.MsOs20VendorCode(code)` on the device, and it must be on both the run-time and the mode interface. The GUID is the interface GUID applications open the device by; generate one once and keep it.

On Linux and macOS no driver is needed; a udev rule may be needed for non-root access, which this stack does not provide.

## Tests

Host tests in `.tool/sim`, run with `bash .tool/sim/run.sh` (all USB tests, including the builder rule snippets):

- `TestDfuDescriptors.cpp`: the run-time and mode configuration bytes against hand-written arrays from DFU 1.1 tables 4.1, 4.2 and 4.4, the device descriptor, the functional descriptor and status response encoding, the poll timeout clamp, the WinUSB BOS, and the MS OS 2.0 set field by field (set header, compatible ID, DeviceInterfaceGUIDs property with its lengths and types), and a mode interface without DetachTimeoutMs() that builds with wDetachTimeOut 0.
- `TestDfuRuntime.cpp`: DFU_DETACH reaching the delegate from `Process()`, the detach timeout and its clamp to wDetachTimeOut, wTimeout 0 taking the descriptor's timeout, a host reset inside the timer delivering a late `Process()`, a reset after the timer dropping the request, WillDetach, the stall in appDETACH, and GETSTATUS in appIDLE.
- `TestDfuState.cpp` (also: the upload that asks for more than wTransferSize ends on the short frame, the busy-state requests during a manifestation, a failed control answer, a layout without one interface, a zero-length download in dfuDNLOAD-SYNC): downloads with a short last block, the poll timeout answers, every write failure code, the sticky error, a target begin failure, the zero-length rules (errNOTDONE), oversized blocks, abort and clear-status rules, unknown requests, a standard request that leaves the state alone, the upload-only and download-only refusals, uploads ending on a short block and on an empty block, manifestation with and without tolerance and WillDetach, a host reset after manifestation restarting the application once, a manifestation error, a bus reset during a write and during a manifestation, a deconfiguration and reconfiguration during a write, a stall during a write that stays in dfuERROR, and a download during a write that stalls.
- `TestDfuAlternates.cpp`: SET_INTERFACE and GET_INTERFACE over the sim host with two and three regions, the stall of an alternate the descriptor does not list, the refusal of a switch in the middle of a download, a download that reaches the target with the selected alternate, and the return to alternate 0 after a bus reset and a reconfiguration.
- `TestDfuTarget.cpp`: the flash example target: the erase as data arrives, the padded last unit, the sequence and region checks, each failure code, the begin rules, the whole-page region rule, the erase guard past the region, the read and verify failures, a program failure after the first unit, the failures inside Manifest(), the flash locked between transfers and after a failed program, the read end, and that it plugs into a `DfuFunction` (construction and layout only; the state machine runs against the fake target in `TestDfuState.cpp`).
- `BuilderErrors/dfu_*.cpp`: the builder rule messages.

## Open

- **The flash example target serves one region.** `DfuFlashTarget::Begin()` accepts alternate 0 only, so a device with `.Region()` and this target refuses a download to region 1 with errTARGET. A device with several regions needs its own `IDfuTarget` that maps each alternate to its memory.
- **GETSTATUS in dfuDNBUSY and dfuMANIFEST answers busy** with the poll timeout. This is a deliberate deviation: appendix A.2.5 and A.2.7 say any class request in those states stalls and moves to dfuERROR. A target may take longer than the timeout it reported, and a stall would fail the host for a slow flash. A host that waits the reported timeout, as the spec's flow has it, never sees the difference. Whether to follow the table exactly is a decision for the application. GETSTATE and the other requests stall as the table says.
- **GETSTATE in dfuDNLOAD-SYNC keeps the state.** The PDF text of appendix A.2.4 is garbled at that row; the state diagram and the other states' rows read as "no transition".
- **dfuMANIFEST-WAIT-RESET stalls the requests.** Appendix A.2.9 says the device does nothing on a class request there. A control request cannot be left unanswered in this stack, so the core stalls it.
- **The flash target's region must be whole pages and units.** `DfuFlashTarget` refuses other regions with errTARGET in `Begin()`. A region that does not fit is a configuration error, not a transfer error, so the host sees it at the first block.
- **Manifestation completes in `Process()`.** The state moves to dfuMANIFEST-SYNC or dfuMANIFEST-WAIT-RESET when the target's `Manifest()` returns, not on a timer. A target that takes longer than its poll timeout is still answered busy.
- **Block numbers are not checked.** wValue (wBlockNum) of DFU_DNLOAD and DFU_UPLOAD is ignored: the stream offset orders the blocks. A host that repeats a block writes it again at the next offset. dfu-util numbers the blocks of a transfer from 0 without repeats.
- **A host that resets during a write and sends DFU_DNLOAD at once gets a stall.** The target is still busy, so the download stalls and the device is in dfuERROR until DFU_CLRSTATUS. The host sees the error and must start again.
- **The detach delegate is the application's.** The stack does not restart the MCU by itself; the application does, in the delegate. `USBDevice::Stop()` and `Start()` keep the same descriptors, so they are not a restart.
- **The reset after manifestation does not check the firmware.** Appendix A.2.9 has the device revert to the application only when the firmware is valid. The stack cannot tell; the delegate decides, and a check belongs in the application's delegate.
