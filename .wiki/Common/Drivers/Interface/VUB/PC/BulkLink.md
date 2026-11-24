# VUB::PC::BulkLink

A vendor bulk interface of a USB device as a reliable byte stream. The device side is `VendorBulkFunction` in buffered mode: two rings, and
each OUT transfer carries one packet, but the transfer boundaries are not kept, so the link is a stream with a framing above it.
Header: `Common/Drivers/Interface/VUB/PC/BulkLink.h`. Namespace `VUB::PC`. See the [PC page](README) for the chain and the reopening rules.

## Traits

`LinkTraits().Reliable().FullDuplex().ReportsState().MaxPacket(256)`. A stream link, reliable. The link is up while the device is open.

Members: `Open(Settings)`, `Close()`, `IsOpen()`, `Write(span)` (a `Result<std::size_t>`), `Poll(Receiver)`, `SetStateSink(StateSink)`. It
satisfies `VUB::StreamLink`, `VUB::PollingLink` and `VUB::ReportingLink`.

## Settings

| Field | Windows | Linux |
|---|---|---|
| `interfaceGuid` | the device interface GUID of the vendor interface, `{8-4-4-4-12}` (required) | not used |
| `vendorId`, `productId` | not used | the USB IDs (required) |
| `interfaceNumber` | not used | the interface to claim |
| `serial` | optional: the serial number of the USB device above the interface; empty matches any | optional: the serial number string of the device; empty matches any |
| `inEndpoint`, `outEndpoint` | the bulk endpoint addresses, default `0x86` and `0x07` | the same |

## Windows: WinUSB

1. `SetupDiGetClassDevsW` with `DIGCF_PRESENT | DIGCF_DEVICEINTERFACE` lists the interfaces of the GUID, and
   `SetupDiGetDeviceInterfaceDetailW` gives each path.
2. The serial is read from the parent of the interface's device node: `CM_Get_Parent`, then `CM_Get_Device_IDW`, and the text after the last
   backslash. An empty serial in the settings matches any device.
3. `CreateFileW` opens the path with `FILE_FLAG_OVERLAPPED`, and `WinUsb_Initialize` gives the interface handle.
4. One overlapped `WinUsb_ReadPipe` of 64 bytes is always pending, into the link's buffer. `Poll` takes the completed read with
   `WinUsb_GetOverlappedResult` and feeds up to the receiver's room; the next read starts once every byte is fed.
5. `Write` copies up to 64 bytes into the link's buffer and starts an overlapped `WinUsb_WritePipe`. It returns zero while the previous write
   is pending. A failed read or write reports down and cancels the transfers in flight; `Poll` releases the device once they have completed.

## Linux: libusb

1. `libusb_init` and `libusb_get_device_list`; the device is matched by `idVendor` and `idProduct`, and its serial string descriptor is read
   when a serial is given.
2. `libusb_open` and `libusb_claim_interface` for the interface number.
3. Asynchronous bulk transfers with the link's buffers: one IN transfer is always in flight, and an OUT transfer is in flight while a write
   is pending. `Poll` and `Write` run `libusb_handle_events_timeout_completed` with a zero timeout, so they never wait. A failed transfer
   reports down and cancels the transfers in flight; `Poll` frees the transfers, releases the interface and closes the handle once the
   cancelled transfers have completed.

## Closing

- `Close` cancels the transfers in flight and waits up to 200 ms for them. A transfer still in flight after that is released by `Poll`,
  which frees the handles once it has completed. `Close` never waits longer than 200 ms.
- A failed read or write, reported from `Poll` or `Write`, does not wait at all: it cancels and reports down, and the release is finished
  by `Poll`.
- `Open` returns `busy` while a release from an earlier close or failure is still waiting for its transfers. The destructor waits until
  every transfer has completed, because the transfers write into the link's own buffers until then.

## Snippet

The snippet below is compiled by `TestVubPcWiki.cpp`. It does not run in a test.

```cpp
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/PC/BulkLink.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <span>

void OnBulkPayload(const VUB::Meta&, std::span<const uint8>) {
}

inline uint32 BulkClockUs() {
	return 0;
}

using BulkStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<VUB::PC::BulkLink>()
		.MaxPayload(64)
		.ReceiveRing(256)
		.OnReceive<&OnBulkPayload>()
		.Clock<&BulkClockUs>();
}>;

// Opens the vendor interface: on Windows by its interface GUID, on Linux by the vendor and product IDs and the interface number.
void RunBulk() {
	VUB::PC::BulkLink::Settings settings;
	settings.interfaceGuid = "{53B72DEF-1477-422A-B645-D5267E3E44A6}";
	settings.vendorId = 0x1209;
	settings.productId = 0x0007;
	settings.interfaceNumber = 4;
	VUB::PC::BulkLink link;
	BulkStack stack(link);
	if (link.Open(settings) == ResultStatus::ok) {
		stack.Process();
	}
}
```

## Test

`TestVubPcBulk.cpp` checks that the link satisfies the stream, polling and reporting concepts, that the serial match is a plain function,
that a GUID is read from its text on Windows, and that a link which is not open takes no bytes. No test opens an interface: the device is
found and opened only on hardware. The host tests compile the Windows and the Linux code paths of the device search and the transfers.
