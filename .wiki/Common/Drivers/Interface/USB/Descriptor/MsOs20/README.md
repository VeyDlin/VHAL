# Microsoft OS 2.0 Descriptors

An optional component. Windows reads these descriptors to bind a driver to the device without an INF file. For this stack, the usual driver is WinUSB. Other hosts ignore the component.

MS OS 2.0 binds WinUSB on Windows 8.1 and later only. Windows 8 and earlier do not read these descriptors, so on them the device binds no driver from the component. Those versions need MS OS 1.0 descriptors or an INF file, and the stack provides neither. A device that must work on them needs an INF for it.

Headers:

- `#include <Drivers/Interface/USB/Descriptor/MsOs20/USBMsOs20DescriptorSet.h>`: the descriptor set, subsets and features
- `#include <Drivers/Interface/USB/Descriptor/MsOs20/USBMsOs20PlatformCapability.h>`: the BOS platform capability

The core has no built-in knowledge of this component. The application adds it to its descriptor set, and the core serves it like any other vendor request.

## How Windows Uses It

1. The device has a BOS. The BOS holds the Microsoft platform capability. That capability gives the vendor code and the length of the descriptor set.
2. Windows reads the BOS. It finds the platform capability and sends a vendor request with that code.
3. The request is an IN request to the device with `wIndex` 7 (`USBMsOs20DescriptorIndex`). The core answers it from `USBDescriptorSet::vendorRequests`.
4. Windows reads the set. It finds the compatible ID (`WINUSB`) and the registry properties, such as the interface GUIDs.

A device with the component needs:

- `bcdUSB` 2.01 or later, because it has a BOS. The example uses 2.10.
- A `USBVendorDescriptor` whose `bRequest` equals the platform capability's vendor code, with `wIndex` 7.
- A device interface GUID that the application generates once and keeps. The GUID is not a fixed value of the stack.

## Builders

| Builder | Result | Description |
|---------|--------|-------------|
| `MakeMsOs20CompatibleId("WINUSB", "")` | 20 bytes | A compatible ID feature. The ID and sub-ID are ASCII, at most 8 characters each. `"WINUSB"` selects WinUSB |
| `MakeUSBUtf16LENullTerminated<USBText{"..."}>()` | Name bytes plus NUL | Used for the names and the values of registry properties |
| `MakeMsOs20MultiStringData(strings...)` | Strings plus a final NUL | REG_MULTI_SZ data. Each string is an `MakeUSBUtf16LENullTerminated` array |
| `MakeMsOs20RegistryProperty<USBText{"Name"}>(type, data)` | 10 + name + data bytes | A registry property feature. `type` is a `USBMsOs20PropertyType` (String, ExpandString, Binary, DwordLittleEndian or MultiString) |
| `MakeMsOs20FunctionSubset(firstInterface, features...)` | 8 bytes plus features | Features for the function whose first interface is `firstInterface`. At least one feature |
| `MakeMsOs20ConfigurationSubset(value, functions...)` | 8 bytes plus subsets | Groups the function subsets of one configuration. Optional. The stack has one configuration, so the example does not use it |
| `MakeMsOs20DescriptorSet(windowsVersion, subsets...)` | 10 bytes plus subsets | The set of a composite device. `windowsVersion` is `0x06030000` for Windows 8.1 |
| `MakeMsOs20DeviceLevelDescriptorSet(windowsVersion, features...)` | 10 bytes plus features | The set of a non-composite device. The compatible ID and registry properties sit directly under the header |
| `MakeMsOs20PlatformCapability(set, vendorCode, altEnumCode)` | 28 bytes | The BOS platform capability. The set's version, its length and the vendor code are read from the set and the arguments. `altEnumCode` is 0 when alternate enumeration is not used |
| `MakeMsOs20VendorDescriptor(vendorCode, set)` | `USBVendorDescriptor` | The entry for `USBDescriptorSet::vendorRequests`. `wIndex` is 7 |

Each element checks its own length field when it is appended. A length that does not match its size fails at compile time.

## Example

The MS OS 2.0 part of the example set. The full set, with the device, configuration and strings, is on the [Descriptor](../README.md#positional-example) page. That device is not composite: its device class is `0x00` and it has one vendor interface, so the features sit directly under the set header:

```cpp
inline constexpr std::array<uint8, 20> MsOsCompatibleId = MakeMsOs20CompatibleId("WINUSB", "");
inline constexpr std::array<uint8, 80> MsOsGuids = MakeMsOs20MultiStringData(
	MakeUSBUtf16LENullTerminated<USBText{"{00000000-0000-0000-0000-000000000000}"}>());
inline constexpr std::array<uint8, 132> MsOsGuidProperty = MakeMsOs20RegistryProperty<USBText{"DeviceInterfaceGUIDs"}>(USBMsOs20PropertyType::MultiString, MsOsGuids);
inline constexpr std::array<uint8, 162> MsOsSet = MakeMsOs20DeviceLevelDescriptorSet(0x06030000, MsOsCompatibleId, MsOsGuidProperty);
inline constexpr std::array<uint8, 28> MsOsPlatform = MakeMsOs20PlatformCapability(MsOsSet, MsOsVendorCode, 0);
inline constexpr std::array<uint8, 33> Bos = MakeUSBBosDescriptor(MsOsPlatform);
inline constexpr USBVendorDescriptor VendorRequests[] = {
	MakeMsOs20VendorDescriptor(MsOsVendorCode, MsOsSet)
};
```

The array sizes are the sizes of the builders' results. The compiler checks them, so a changed name or GUID needs new sizes. A GUID in the registry property is UTF-16 with its braces. The example GUID is a placeholder, and a product uses its own.

Wire these into the set:

```cpp
inline constexpr USBDescriptorSet Descriptors = {
	DeviceDescriptor,
	Configuration,
	Strings,
	Bos,
	VendorRequests
};
```

## Layout of the Set

| Element | Size | Length field |
|---------|------|--------------|
| Set header | 10 bytes | `wLength` (10) at offset 0, type 0 at offset 2, `dwWindowsVersion` at offset 4, `wTotalLength` at offset 8 |
| Function subset | 8 bytes plus features | `wSubsetLength` at offset 6 |
| Compatible ID | 20 bytes | `wLength` at offset 0 |
| Registry property | 10 bytes plus name and data | `wLength` at offset 0 |

The platform capability holds the set's `wTotalLength` and the vendor code, so the BOS and the set must be built from the same value. The builders read both from the set, so they cannot disagree.

## Notes

- The vendor code is the application's choice. The stack has no default. Any value that is not used by another vendor request on the device works.
- The example uses the same vendor code in the platform capability and in `MakeMsOs20VendorDescriptor`. The core matches the request by that code and `wIndex`.
- The set must stay in static storage, because `USBVendorDescriptor` keeps a span to it.
- Windows sends the set request as a vendor IN request (`bmRequestType` `0xC0`, `wIndex` 7). The core answers it from the set. The OUT form is stalled.
- A device that is not composite (device class `0x00`, one function) must put the features directly under the set header with `MakeMsOs20DeviceLevelDescriptorSet`. Windows applies a function subset only through its composite driver, so on such a device a compatible ID inside a function subset binds no driver (checked on an STM32G431 with Windows 11: no `USB\MS_COMP_WINUSB` among the compatible IDs until the set was changed). A composite device uses `MakeMsOs20DescriptorSet` with one function subset per function.
- Verified on hardware: Windows 11 binds WinUSB from the device-level set with no INF and writes `DeviceInterfaceGUIDs` to the device's registry key. The byte layouts are checked against hand-written bytes in `.tool/sim/TestDescriptorBuilders.cpp`.
