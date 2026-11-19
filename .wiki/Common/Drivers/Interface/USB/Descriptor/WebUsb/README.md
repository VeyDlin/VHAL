# WebUSB

An optional component. A browser that implements WebUSB (Chrome and Edge) can open the device from a web page through `navigator.usb`, once the user grants access. The device names a landing page URL, which the browser shows for the device. Other hosts ignore the component.

Headers:

- `#include <Drivers/Interface/USB/Descriptor/WebUsb/USBWebUsbPlatformCapability.h>`: the BOS platform capability and the GET_URL vendor request
- `#include <Drivers/Interface/USB/Descriptor/WebUsb/USBWebUsbUrlDescriptor.h>`: the URL descriptor

The core has no built-in knowledge of this component. The device builder adds it when the chain has `.WebUsb(...)`, and the core serves GET_URL like any other vendor request.

## How the Browser Uses It

1. The device has a BOS. The BOS holds the WebUSB platform capability. That capability gives the vendor code and the index of the landing page URL.
2. The browser sends GET_URL: an IN vendor request to the device with `bRequest` set to the vendor code, `wValue` set to the URL index, and `wIndex` 2 (`USBWebUsbRequestIndex`).
3. The core answers from `USBDescriptorSet::vendorRequests`. The answer is the URL descriptor, truncated to `wLength`. An index that does not exist stalls.

A device with WebUSB needs:

- `bcdUSB` 2.10 or later. The builder selects 2.10 when the chain has a BOS, and a lower `UsbVersion()` is a compile error. Chrome's guide places the BOS in USB 2.1; the WebUSB specification sets no minimum, so this is the builder's rule, not a spec requirement.
- A function that asks for WinUSB on Windows (see below).
- A vendor code. It may be the same as the MS OS 2.0 vendor code or a different one. The core tells the two requests apart by `wIndex` (2 for GET_URL, 7 for the MS OS 2.0 set).

## Windows and Linux

Chrome on Windows talks to the device through WinUSB. The interface must be bound to WinUSB, or `navigator.usb` finds nothing it can open. For that reason a WebUSB function also asks for WinUSB: call `.WinUsb()` on its class builder, and give the device `.MsOs20VendorCode(...)`. Windows then binds WinUSB without an INF file.

On Linux, the browser opens the device node under `/dev/bus/usb`, which belongs to root by default. A udev rule gives the logged-in user access. Replace the vendor and product IDs with the device's own:

```
SUBSYSTEM=="usb", ATTR{idVendor}=="1209", ATTR{idProduct}=="0001", TAG+="uaccess"
```

Save it as `/etc/udev/rules.d/99-webusb.rules`, then run `sudo udevadm control --reload-rules && sudo udevadm trigger` and reconnect the device. Systems without systemd can use `GROUP` and `MODE` in the rule instead of `uaccess`.

## Builders

| Builder | Result | Description |
|---------|--------|-------------|
| `.WebUsb(vendorCode, "https://...")` | Device builder step | Adds the platform capability to the BOS, forces bcdUSB 2.10 or later, and adds the GET_URL entry to the vendor requests. The URL is given once |
| `MakeWebUsbPlatformCapability(vendorCode, landingPageIndex)` | 24 bytes | The BOS platform capability. `bcdVersion` is 1.00. `landingPageIndex` is 1 for the single URL; 0 means no landing page |
| `MakeWebUsbUrlDescriptor<USBText{"..."}>()` | 3 bytes plus the URL | The URL descriptor of a literal. An `https://` or `http://` prefix becomes the scheme code, and the rest of the text is sent |
| `MakeWebUsbVendorDescriptor(vendorCode, landingPageIndex, url)` | `USBVendorDescriptor` | The GET_URL entry for `USBDescriptorSet::vendorRequests`. `wIndex` is 2, and `wValue` must equal `landingPageIndex` |

## Example

```cpp
using AppUsb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("VHAL")
		.Product("WebUSB example")
		.SerialNumber("0001")
		.MsOs20VendorCode(0x42)
		.WebUsb(0x43, "https://example.com/")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBVendorBuilder()
				.BulkIn(0x81)
				.BulkOut(0x01)
				.WinUsb()));
}>;
```

The builder checks these rules, and a violation is a compile error that names the rule:

- The URL is valid UTF-8 and has text after its `https://` or `http://` prefix, at most 252 bytes of it. The descriptor's `bLength` is one byte.
- Only lowercase `https://` and `http://` prefixes are shortened. An upper-case prefix is sent whole with scheme 255.
- `.WebUsb(...)` is given once, with a non-empty URL.
- `bcdUSB` is 0x0210 or later.

## Layout

The platform capability (24 bytes):

| Offset | Size | Field | Value |
|--------|------|-------|-------|
| 0 | 1 | `bLength` | 24 |
| 1 | 1 | `bDescriptorType` | 16 (device capability) |
| 2 | 1 | `bDevCapabilityType` | 5 (platform) |
| 3 | 1 | `bReserved` | 0 |
| 4 | 16 | `PlatformCapabilityUUID` | `{3408B638-09A9-47A0-8BFD-A0768815B665}` in wire byte order |
| 20 | 2 | `bcdVersion` | 0x0100 |
| 22 | 1 | `bVendorCode` | the vendor code |
| 23 | 1 | `iLandingPage` | the landing page index |

The URL descriptor:

| Offset | Size | Field | Value |
|--------|------|-------|-------|
| 0 | 1 | `bLength` | 3 plus the URL bytes |
| 1 | 1 | `bDescriptorType` | 3 (WEBUSB_URL) |
| 2 | 1 | `bScheme` | 0 for `http://`, 1 for `https://`, 255 for no prefix |
| 3 | n | URL | UTF-8 text after the prefix, no terminator |

## Notes

- The BOS holds the WebUSB capability after the MS OS 2.0 capability when both are present. Each has its own vendor code field, so the order does not matter to the host.
- The core matches GET_URL on `wValue` as well as `wIndex`. A request for an index other than the landing page index stalls. The MS OS 2.0 entry matches any `wValue`.
- The URL descriptor is in static storage, because the core keeps a span to it. The builder places it there with the rest of the descriptor set.
- The vendor code is the application's choice. The stack has no default.
