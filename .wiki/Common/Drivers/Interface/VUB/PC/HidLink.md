# VUB::PC::HidLink

A HID device as a packet link, with the report layout of the device's `HidLink` (`Link/HidReport.h`): byte 0 is the length of the payload,
the payload follows it, and the rest of the report is zero.
Header: `Common/Drivers/Interface/VUB/PC/HidLink.h`. Namespace `VUB::PC`. See the [PC page](README) for the chain and the reopening rules.

## Traits

`HidLink<ReportBytes = 64>` has `LinkTraits().Framed().FullDuplex().ReportsState().MaxPacket(ReportBytes - 1)`. A packet link: it is framed
(a packet is one report) and it is not reliable, since the host may drop input reports it does not read. A 64-byte report carries 63 bytes
of packet.

Members: `Open(vendorId, productId, usagePage)`, `Close()`, `IsOpen()`, `Send(span)` (a `ResultStatus`), `Poll(Receiver)`,
`SetStateSink(StateSink)`, and the static `PackOutput` and `UsagePageOf`. It satisfies `VUB::PacketLink`, `VUB::PollingLink` and
`VUB::ReportingLink`.

## Report layout

- An input report of the device is `ReportBytes` long, with no report ID: the length byte, the payload, zero padding. `HidReportPayload`
  gives the payload.
- An output report has the ID 0 in front, so it is `ReportBytes + 1` bytes: `PackOutput` writes the ID, the length byte, the payload and the
  padding. It refuses an empty payload and a payload that does not fit in `ReportBytes - 1` bytes.

## Windows: hid.dll and SetupAPI

1. `HidD_GetHidGuid` gives the HID interface GUID, and the interfaces are listed with `SetupDiGetClassDevsW` and
   `SetupDiGetDeviceInterfaceDetailW`.
2. Each candidate is opened with `CreateFileW` and `FILE_FLAG_OVERLAPPED`. It is taken when `HidD_GetAttributes` gives the vendor and product
   IDs, and `HidP_GetCaps` (from `HidD_GetPreparsedData`) gives the usage page; a usage page of zero matches any.
3. The report lengths of the OS are read from the caps. Each must be `ReportBytes`, or `ReportBytes + 1` with the ID byte in front of the
   report. The read and the write use the length the caps give.
4. One overlapped `ReadFile` of an input report is pending into the link's buffer. A report that is complete is held until the receiver has
   room, then fed as a packet.
5. `Send` writes the output report with an overlapped `WriteFile`. It returns `busy` while the previous write is pending, and `ok` once the
   report is on its way.

## Linux: hidraw

1. The nodes `/dev/hidraw0` to `/dev/hidraw63` are opened with `O_NONBLOCK`. `HIDIOCGRAWINFO` gives the vendor and product IDs of each.
2. When a usage page is given, it is read from the report descriptor (`HIDIOCGRDESCSIZE` and `HIDIOCGRDESC`): the first Usage Page item of
   the descriptor. Zero matches any usage page.
3. `Poll` reads one report with `read` when none is held, and feeds it when the receiver has room. `Send` writes the output report with
   `write` and returns `busy` when the kernel does not take it now.

On Linux the output report is written with its ID byte 0 in front, as the Windows path does. The kernel's hidraw driver sends an unnumbered
report without that byte. This is not yet checked on a device.

## Errors

A read or write that fails closes the device and reports down. `Open` returns `notFound` when no device matches the IDs, the usage page and
(on Windows) the report lengths. A device that is found but cannot be opened gives `deviceOrResourceBusy`.

## Snippet

The snippet below is compiled by `TestVubPcWiki.cpp`. It does not run in a test.

```cpp
#include <Drivers/Interface/VUB/PC/HidLink.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <span>

void OnReportPayload(const VUB::Meta&, std::span<const uint8>) {
}

inline uint32 ReportClockUs() {
	return 0;
}

// A packet link: there is no framing, and a 64-byte report carries 63 bytes of packet.
using ReportStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Link<VUB::PC::HidLink<64>>()
		.MaxPayload(60)
		.ReceiveQueue(4)
		.OnReceive<&OnReportPayload>()
		.Clock<&ReportClockUs>();
}>;

// Opens the first device with the vendor ID, product ID and the vendor usage page 0xFF00.
void RunHid() {
	VUB::PC::HidLink<64> link;
	ReportStack stack(link);
	if (link.Open(0x1209, 0x0007, 0xFF00) == ResultStatus::ok) {
		stack.Process();
	}
}
```

## Test

`TestVubPcHid.cpp` checks, on both systems, the concepts and the traits (a 64-byte report carries 63 bytes), the output layout with the ID
0 in front and the length byte, the refusal of an empty or too long packet, the usage page of a descriptor (the first Usage Page item, and a
descriptor that is cut short gives zero), and that a link which is not open refuses to send. No test opens a device.
