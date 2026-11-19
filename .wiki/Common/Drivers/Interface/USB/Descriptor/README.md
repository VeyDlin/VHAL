# USB Descriptors

The descriptors of a device are written with a fluent builder. The builder runs at compile time: the chain is evaluated in a constant expression, and only the resulting bytes reach flash. The positional builders (`Make*` functions) are the byte layer the fluent builder is made of. Use them for a hand-written descriptor, or for a value the fluent builder does not cover yet.

Headers:

- `#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>` for `USBDescriptors`, `USBDeviceBuilder` and `USBConfigurationBuilder`
- `#include <Drivers/Interface/USB/Class/Vendor/USBVendorBuilder.h>` for the vendor bulk class builder
- `#include <Drivers/Interface/USB/Descriptor/USBSerialNumber.h>` for the run-time serial number helper
- `#include <Drivers/Interface/USB/Descriptor/USBDescriptorSet.h>` for `USBDescriptorSet`, the only thing the core reads
- the header of each positional builder, listed under [Positional Builders](#positional-builders-the-byte-layer)

The device core reads a `USBDescriptorSet`. The fluent builder gives one through `USBDescriptors<...>::Set`.

## Fluent Builder

### Usage

The application writes the chain inside a captureless lambda and passes it as a template argument. The result is a class template whose static members are the arrays the chain produced:

```cpp
#include <Drivers/Interface/USB/Class/Vendor/USBVendorBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>

using AppUsb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)                 // the product's own VID and PID; 0x1209/0x0001 is a test pair
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Example")
		.Product("Vendor Bulk Device")
		.SerialNumberAtRuntime()          // the serial number comes from the MCU at run time
		.MsOs20VendorCode(0x20)
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBVendorBuilder()
				.Name("Vendor bulk")
				.BulkIn(0x81)
				.BulkOut(0x02)
				.WinUsb("{00000000-0000-0000-0000-000000000000}")));
}>;
```

The GUID is a placeholder. Generate one for each product and keep it: Windows finds the driver by it.

The device uses the result like this:

```cpp
VendorBulkFunction<4096, 4096> vendor(AppUsb::Function(0));
USBFunction* functions[] = { &vendor };
USBDevice device(BSP::usbAdapter, AppUsb::Set, functions);
```

`AppUsb::Set` is the `USBDescriptorSet` view. `AppUsb::Function(index)` is the layout of one function, in the order the configuration added them. Both point into static storage, so the device and the function keep valid spans.

### Result: `USBDescriptors<Make>`

| Member | Type | Description |
|--------|------|-------------|
| `Set` | `USBDescriptorSet` | The set for `USBDevice`. Includes `serialNumberAtRuntime` |
| `Function(index)` | `USBFunctionLayout` | The interfaces, endpoint addresses and descriptor slice of function `index`. `consteval`: the index must be a constant |
| `Device` | `std::array<uint8, 18>` | The device descriptor |
| `Configuration` | `std::array<uint8, N>` | The configuration descriptor with all its interfaces and endpoints |
| `StringTable` | `std::array<std::span<const uint8>, N>` | The string descriptors, index 0 is the language list |
| `Bos` | `std::array<uint8, N>` | The BOS. Empty size 0 when the chain has no MS OS 2.0 part |
| `MsOs20Set` | `std::array<uint8, N>` | The MS OS 2.0 descriptor set. Empty when the chain has none |
| `VendorRequests` | `std::array<USBVendorDescriptor, 0 or 1>` | The vendor request table of the set |
| `InterfaceNumbers`, `EndpointAddresses` | `std::array<uint8, N>` | The numbers the configuration assigned, in order |

The members are public, so an application can `static_assert` on them. The bench firmware does this for its offsets.

### Device Builder: `USBDeviceBuilder`

| Method | Required | Description |
|--------|----------|-------------|
| `VendorId(uint16)` | Yes | idVendor |
| `ProductId(uint16)` | Yes | idProduct |
| `DeviceRelease(uint16)` | Yes | bcdDevice |
| `UsbVersion(uint16)` | No | bcdUSB. Without it the builder picks 0x0210 when it writes a BOS, else 0x0200. A BOS needs 0x0201 or later, and the rule is checked against any value given here |
| `Usb2Extension()` | No | Adds the USB 2.0 extension capability to the BOS, and so builds a BOS. Off by default |
| `Language(uint16)` | No | Language ID of the string descriptors. Default 0x0409 |
| `EndpointNumberLimit(uint8)` | No | Highest endpoint number the device uses, 1 to 15. Default 15. Use the controller's limit: 7 for STM32 `fsdev` |
| `Manufacturer(text)` | Yes | iManufacturer. There is no default |
| `Product(text)` | Yes | iProduct. There is no default |
| `SerialNumber(text)` | Yes, or `SerialNumberAtRuntime()` | iSerialNumber, a fixed string. Given once, and not together with `SerialNumberAtRuntime()` |
| `SerialNumberAtRuntime()` | No | Reserves iSerialNumber for a string the application supplies with `USBDevice::SetSerialNumber()` before `Start()`. See [Runtime Serial Number](#runtime-serial-number) |
| `MsOs20VendorCode(uint8)` | When a function asks for WinUSB | bMS_VendorCode of the MS OS 2.0 descriptors. Writing it also writes the BOS. No default |
| `Configuration(USBConfigurationBuilder)` | Yes | The configuration. Exactly one. Its strings and endpoints join the device |

### Configuration Builder: `USBConfigurationBuilder`

| Method | Required | Description |
|--------|----------|-------------|
| `MaxPowerMilliamps(uint16)` | Yes | Bus current in mA, at most 500, and even: bMaxPower counts 2 mA steps |
| `SelfPowered()` | No | Sets the self-powered bit |
| `RemoteWakeup()` | No | Sets the remote wakeup bit in the configuration |
| `Function(classBuilder)` | At least one | Adds a function. Takes any class builder that meets the concept below |

The configuration builder numbers the interfaces and the endpoints as each function is added. It writes an interface association before the function's descriptors when the function has two or more interfaces.

### Class Builders

A class builder is a class that meets `USBClassFunctionBuilder`. The configuration builder calls these members, in this order:

| Member | Returns | Purpose |
|--------|---------|---------|
| `InterfaceCount()` | count | Interfaces the class uses. At least 1 |
| `EndpointCount()`, `Endpoint(i)` | count, `USBEndpointRequest` | Endpoints the class needs, in the order its driver expects. A request has a type, a direction, an optional address, and a packet size and interval |
| `StringCount()`, `String(i)` | count, text | Interface names, at most 4 |
| `FunctionClass()`, `FunctionSubClass()`, `FunctionProtocol()` | `uint8` | Class of the interface association |
| `HasWinUsb()`, `WinUsbGuid()` | `bool`, text | Binds WinUSB. An empty GUID gives the compatible ID only |
| `Emit(emitter)` | none | Writes the interface, class-specific and endpoint descriptors through the emitter |

The emitter gives the class its numbers, so the class never handles them itself:

- `InterfaceNumber(i)`, `EndpointAddress(i)`: the numbers the configuration resolved
- `Interface(i, class, subClass, protocol, stringIndex)`: an interface descriptor. `stringIndex` is an index into the class's own strings, or `USBNoString`
- `Endpoint(i)`: the endpoint descriptor for request `i`
- `Raw(bytes)`: a class-specific descriptor, such as a functional descriptor. Its `bLength` must equal its size, and it cannot be a configuration, interface, endpoint or association

`USBVendorBuilder` is the vendor bulk class: one interface of class 0xFF with a bulk IN and a bulk OUT endpoint, 64-byte packets, interval 0.

| Method | Description |
|--------|-------------|
| `Name(text)` | The interface name |
| `BulkIn(address)` | The IN address, bit 7 set, endpoint number 1 to 15. Left out: assigned by the builder |
| `BulkOut(address)` | The OUT address, bit 7 clear, endpoint number 1 to 15. Left out: assigned by the builder. `BulkOut(0x00)` is rejected: it is endpoint 0, the control pipe |
| `WinUsb()` | Binds WinUSB. Needs `MsOs20VendorCode()` on the device |
| `WinUsb(guid)` | Binds WinUSB and names the interface GUID in registry form, `"{...}"`. The applications open the device with that GUID |

The layout's endpoint list is IN first, then OUT. That is the order `VendorBulkFunction` expects.

### Interface and Endpoint Numbering

- Interfaces are numbered from 0 in the order the functions are added.
- An endpoint with an explicit address keeps it.
- An endpoint without an address takes the smallest number from 1 for which neither the IN nor the OUT endpoint is already used. So IN and OUT never share a number. On fsdev a shared number puts both endpoints in one register and disables double buffering.
- An endpoint number above `EndpointNumberLimit` is a compile error when the configuration joins the device.
- A duplicate address, an explicit address whose direction differs from the request, a reserved address bit, and endpoint 0 are compile errors.
- A class requests bulk or interrupt endpoints only. An isochronous request is a compile error.

### Strings

- Each distinct text is one string descriptor. A text used twice is one entry.
- Strings are numbered in the order the chain first uses them. A device string takes its number when its method is called. A configuration's strings take theirs when `Configuration()` is called. So a string given before `Configuration()` has a lower number than the configuration's own strings.
- The language list is string 0. Its language is 0x0409 unless `Language()` is given.
- A string must be valid UTF-8, at most 126 UTF-16 code units, and not empty.

### Composite Rules

- A function with two or more interfaces gets an interface association descriptor.
- When any association is present, the device class is 0xEF/0x02/0x01. Otherwise it is 0x00/0x00/0x00.
- A BOS is written when `MsOs20VendorCode()` or `Usb2Extension()` is given. Then bcdUSB is 0x0210 unless `UsbVersion()` says otherwise, and it must be 0x0201 or later.
- The maximum power is at most 500 mA.
- There is one configuration, so `Configuration()` can be called once.

### MS OS 2.0 from the Builder

A function asks for WinUSB with `WinUsb()` or `WinUsb(guid)`. The builder then writes the MS OS 2.0 descriptors and the BOS platform capability for the device:

- The vendor code, `MsOs20VendorCode()`, is required when any function asks for WinUSB. There is no default.
- The set asks for Windows 8.1 (0x06030000).
- With one interface in the whole configuration, the compatible ID and the GUID property stand at device level.
- With more interfaces, each WinUSB function gets its own function subset, at its first interface. Only the WinUSB interfaces bind to WinUSB; the others keep their own drivers.
- A GUID given with `WinUsb(guid)` is written as the `DeviceInterfaceGUIDs` property. Without a GUID, only the compatible ID is written.

Other hosts ignore the MS OS 2.0 descriptors. See [Descriptor/MsOs20](MsOs20/README.md) for the byte layout.

### Runtime Serial Number

Some devices need a serial number that is known only on the device, such as the MCU's unique ID. The application does this:

1. Build the chain with `SerialNumberAtRuntime()`. The set then reserves iSerialNumber, and `Set.serialNumberAtRuntime` is true.
2. Before `Start()`, build the string descriptor from the unique ID with `MakeUSBSerialNumberDescriptor()`, into an array of static storage. Then call `USBDevice::SetSerialNumber()` with it.
3. Call `Start()`.

```cpp
static std::array<uint8, 2 + 4 * 12> serialDescriptor{};

// Thread context, before the first Start(). The 96-bit unique ID of the STM32G4 is at UID_BASE.
ResultStatus SetUniqueSerial() {
	std::array<uint8, 12> uniqueId{};
	const volatile uint8* source = reinterpret_cast<const volatile uint8*>(UID_BASE);
	for (std::size_t index = 0; index < uniqueId.size(); index++) {
		uniqueId[index] = source[index];
	}

	ResultStatus status = MakeUSBSerialNumberDescriptor(uniqueId, serialDescriptor);
	if (status != ResultStatus::ok) {
		return status;
	}

	return device.SetSerialNumber(serialDescriptor);
}
```

- `MakeUSBSerialNumberDescriptor()` writes the bytes of the ID as upper-case hex, two characters per byte, in UTF-16LE. The same ID gives the same text, so the host finds the device again after a reboot. It returns `invalidParameter` when the buffer is too small, or when the descriptor would be longer than 255 bytes (more than 63 bytes of ID).
- `SetSerialNumber()` keeps the span, so the array must have static storage. It returns `busy` after `Start()`, and `invalidParameter` for a descriptor that is not a string descriptor of the right length, for an empty string, and for a set that does not reserve the run-time serial.
- `Start()` returns `invalidParameter` when the set reserves iSerialNumber and no serial number was given, and when the device descriptor's iSerialNumber is 0.
- Without `SerialNumberAtRuntime()`, the set does not need `SetSerialNumber()`, and the call is refused. A static serial given with `SerialNumber()` is never replaced.

The bench firmware (`.demo/STM32G4/Application/DemoUSBBench/DemoUSBBench.cpp`) does exactly this, so several boards of one firmware are told apart on one PC.

### Compile-Time Errors

A rule the chain breaks calls a function that is declared but never defined, with the message as a template argument. The compiler reports that call as an error, so the message is in the diagnostic. For example, this chain has an odd `MaxPowerMilliamps()`:

```cpp
using Usb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(101)
			.Function(USBVendorBuilder().BulkIn(0x81).BulkOut(0x02)));
}>;
```

GCC 15 reports (the first line of the diagnostic):

```text
error: call to non-'constexpr' function 'void DescriptorError() [with USBText<...auto...> Message = USBText<50>{"bMaxPower counts 2 mA steps, so the value is even"}]'
```

The lines after it trace the instantiations back to the chain, and point at the `MaxPowerMilliamps(101)` call. The message is the text the rule gives, so the diagnostic names the rule that failed. Other rules give their own message, such as `"VendorId is required"`, `"an endpoint number is above EndpointNumberLimit"` or `"MsOs20VendorCode is required when a function asks for WinUSB"`. Only GCC has been checked this way.

The rule checks sit in `.tool/sim/BuilderErrors/`: one snippet per rule, each with its expected message on its first line. `run.sh` fails when a snippet compiles or reports another message.

### Limits

| Item | Limit |
|------|-------|
| Functions in one configuration | 8 |
| Endpoints in one configuration | 32 |
| Endpoints in one function | 16 |
| Strings in one device | 32, including the serial placeholder |
| Strings in one function | 4 |
| Bytes of one part (configuration, strings, BOS, MS OS 2.0 set) | 1024 |
| Interfaces | 256, the one-byte numbers |

A limit that is exceeded is a compile error.

## Positional Builders (the Byte Layer)

The positional builders produce one descriptor each, from its fields. The fluent builder calls them, so their output is what the fluent builder produces. Use them for a hand-written descriptor, or to check a single field.

Each builder is `constexpr`. Its result is an array of bytes, in flash when the call is a constant expression. A bad input does not compile. The compiler error names the rule that failed.

### How the Checks Work

- The fluent builder checks each rule with `Require<USBText{"reason"}>(condition)`. The reason is a template argument, so the compiler error line carries it. A false condition calls a function that is declared, never defined, so a constant expression that reaches it does not compile.
- The positional builders check with `USBDescriptorRequire(condition, reason)`. A false condition calls `USBDescriptorRejected(reason)`, also declared and never defined. Their reason is printed in a note, not in the error line.
- A call that is not a constant expression can fail at link time, because the failing branch refers to an undefined function unless the optimiser removes it. Always assign the result to a `constexpr` or `inline constexpr` variable, so the checks run at compile time.
- The composition checks are `static_assert`s. Each names the rule that failed.
- No value has a default. The application gives VID, PID, bcdDevice, strings and the serial number.

### Builders

| Builder | Result | Checks |
|---------|--------|--------|
| `MakeUSBDeviceDescriptor(bcdUSB, class, subClass, protocol, maxPacketSize0, idVendor, idProduct, bcdDevice, iManufacturer, iProduct, iSerialNumber, numConfigurations)` | 18 bytes | `bcdUSB` 2.00 or later. `maxPacketSize0` is 64. `numConfigurations` is 1 |
| `MakeUSBInterfaceDescriptor(number, alternate, class, subClass, protocol, iInterface)` | 9 bytes | `bNumEndpoints` is set by the configuration builder |
| `MakeUSBEndpointDescriptor(address, type, maxPacketSize, interval)` | 7 bytes | Not endpoint 0. Bits 6 to 4 of the address are 0. Bulk: 8, 16, 32 or 64 bytes and interval 0. Interrupt: 1 to 64 bytes and interval 1 or more. No isochronous |
| `MakeUSBInterfaceAssociationDescriptor(firstInterface, count, class, subClass, protocol, iFunction)` | 8 bytes | `count` is 1 or more, and the range stays within 0 to 255 |
| `MakeUSBRawDescriptor<Size>({ ... })` | `Size` bytes | `bLength` equals the size. Size 2 to 255. Not a configuration, interface, endpoint or association. For class-specific bytes |
| `MakeUSBConfigurationDescriptor(value, iConfiguration, selfPowered, remoteWakeup, maxPower, pieces...)` | 9 bytes plus the pieces | `value` is 1 or more. `maxPower` is 250 or less (units of 2 mA). Computes `wTotalLength`, `bNumInterfaces` and each `bNumEndpoints`. Checks that each endpoint follows its interface and that association and interface numbers agree |
| `MakeUSBLanguageIdDescriptor(languageId)` | 4 bytes | Not 0. This is string descriptor 0 |
| `MakeUSBStringDescriptor<USBText{"..."}>()` | `2 + 2 * units` bytes | UTF-8 input, no surrogates, at most 126 UTF-16 code units |
| `MakeUSBUtf16LE<USBText{"..."}>()` | `2 * units` bytes | Text as UTF-16LE, no terminator |
| `MakeUSBUtf16LENullTerminated<USBText{"..."}>()` | `2 * (units + 1)` bytes | As above, with a 16-bit NUL. For registry names and REG_SZ values |
| `USBDeviceClassMatchesConfiguration(device, configuration)` | `bool` | Class `0xEF/0x02/0x01` exactly when the configuration has an association. Otherwise `0x00/0x00/0x00` |
| `USBDeviceSupportsBos(device)` | `bool` | `bcdUSB` is 2.01 or later |

#### Strings

`USBText` holds a string literal as a template argument. Its length is part of the type, so the builders size the array exactly. Write strings as `MakeUSBStringDescriptor<USBText{"Name"}>()`.

Strings must be valid UTF-8. Overlong forms, surrogate code points and code points above U+10FFFF are rejected. Code points above U+FFFF are written as surrogate pairs.

String 0 is the language list. Use `MakeUSBLanguageIdDescriptor(0x0409)` for English (United States). The core answers string index `n` with `strings[n]`. An index past the end is stalled.

#### Configuration and Pieces

`MakeUSBConfigurationDescriptor()` takes the interface and endpoint arrays in order. Each endpoint must follow its interface. An endpoint address must be unique across the configuration. `bNumInterfaces` counts the alternate-0 interfaces only.

`MakeUSBRawDescriptor()` takes the class-specific bytes. Its first byte must be the length.

#### BOS

A BOS descriptor holds device capabilities. `MakeUSBBosDescriptor(caps...)` builds it from at least one capability. The helpers are:

| Builder | Result |
|---------|--------|
| `MakeUSBUsb2ExtensionCapability(linkPowerManagement)` | 7 bytes. The USB 2.0 extension. Link power management is off in the stack, so pass `false` |
| `MakeUSBPlatformCapability(uuid, data)` | 20 bytes plus the data. A platform capability with its 16-byte UUID. The data layout belongs to the platform |
| `MakeUSBBosDescriptor(caps...)` | 5 bytes plus the capabilities. `wTotalLength` and `bNumDeviceCaps` are computed |

The BOS is optional. Without one, the core stalls GET_DESCRIPTOR(BOS). A BOS needs a device with `bcdUSB` 2.01 or later.

The fluent builder writes the USB 2.0 extension only when the chain calls `Usb2Extension()`, and then before the MS OS 2.0 platform capability. Without it the builder's BOS is the platform capability alone, so the bench device's BOS matches its hand-written set byte for byte. A hand-written BOS may include the extension or leave it out.

### Positional Example

This is the descriptor set of the vendor bulk example, written with the positional builders. It has one vendor interface with a bulk IN endpoint `0x81` and a bulk OUT endpoint `0x02`, and a BOS with the Microsoft OS 2.0 platform capability. The MS OS 2.0 part is explained on the [Descriptor/MsOs20](MsOs20/README.md) page.

The example takes its VID and PID from the application, not from the library. A product uses its own VID and PID, strings and GUID. The test pair used by the bench tools is in `.tool/common/BenchProtocol.h`, for tests only. The GUID in this example is a placeholder. Generate a real one for a product.

```cpp
#pragma once
#include <Drivers/Interface/USB/Class/Vendor/VendorBulkFunction.h>
#include <Drivers/Interface/USB/Class/Vendor/VendorBulkStreamFunction.h>
#include <Drivers/Interface/USB/Descriptor/MsOs20/USBMsOs20DescriptorSet.h>
#include <Drivers/Interface/USB/Descriptor/MsOs20/USBMsOs20PlatformCapability.h>
#include <Drivers/Interface/USB/Descriptor/USBBosDescriptor.h>
#include <Drivers/Interface/USB/Descriptor/USBConfigurationDescriptor.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorComposition.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorSet.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorString.h>
#include <Drivers/Interface/USB/Descriptor/USBDeviceDescriptor.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <Drivers/Interface/USB/Device/USBFunction.h>


namespace PositionalUsb {


// Vendor code of the MS OS 2.0 request, the bRequest that fetches the set. Any value that no other vendor request uses.
inline constexpr uint8 MsOsVendorCode = 0x20;


// VendorId and ProductId are the product's own pair, defined by the application. A product never ships a test pair.
inline constexpr std::array<uint8, 18> DeviceDescriptor = MakeUSBDeviceDescriptor(
	0x0210,
	0x00,
	0x00,
	0x00,
	64,
	VendorId,
	ProductId,
	0x0100,
	1,
	2,
	3,
	1);


inline constexpr std::array<uint8, 4> LanguageId = MakeUSBLanguageIdDescriptor(0x0409);
inline constexpr std::array<uint8, 16> Manufacturer = MakeUSBStringDescriptor<USBText{"Example"}>();
inline constexpr std::array<uint8, 38> Product = MakeUSBStringDescriptor<USBText{"Vendor Bulk Device"}>();
inline constexpr std::array<uint8, 10> SerialNumber = MakeUSBStringDescriptor<USBText{"0001"}>();
inline constexpr std::span<const uint8> Strings[] = {
	LanguageId,
	Manufacturer,
	Product,
	SerialNumber
};


inline constexpr std::array<uint8, 9> Interface = MakeUSBInterfaceDescriptor(0, 0, 0xFF, 0x00, 0x00, 0);
inline constexpr std::array<uint8, 7> BulkIn = MakeUSBEndpointDescriptor(0x81, USBEndpointType::Bulk, 64, 0);
inline constexpr std::array<uint8, 7> BulkOut = MakeUSBEndpointDescriptor(0x02, USBEndpointType::Bulk, 64, 0);
inline constexpr std::array<uint8, 32> Configuration = MakeUSBConfigurationDescriptor(1, 0, false, true, 50, Interface, BulkIn, BulkOut);


inline constexpr std::array<uint8, 20> MsOsCompatibleId = MakeMsOs20CompatibleId("WINUSB", "");
// Placeholder interface GUID. Generate one once for the product and keep it: Windows matches the driver by it.
inline constexpr std::array<uint8, 80> MsOsGuids = MakeMsOs20MultiStringData(
	MakeUSBUtf16LENullTerminated<USBText{"{00000000-0000-0000-0000-000000000000}"}>());
inline constexpr std::array<uint8, 132> MsOsGuidProperty = MakeMsOs20RegistryProperty<USBText{"DeviceInterfaceGUIDs"}>(USBMsOs20PropertyType::MultiString, MsOsGuids);
inline constexpr std::array<uint8, 160> MsOsFunction = MakeMsOs20FunctionSubset(0, MsOsCompatibleId, MsOsGuidProperty);
inline constexpr std::array<uint8, 170> MsOsSet = MakeMsOs20DescriptorSet(0x06030000, MsOsFunction);
inline constexpr std::array<uint8, 28> MsOsPlatform = MakeMsOs20PlatformCapability(MsOsSet, MsOsVendorCode, 0);
inline constexpr std::array<uint8, 33> Bos = MakeUSBBosDescriptor(MsOsPlatform);
inline constexpr USBVendorDescriptor VendorRequests[] = {
	MakeMsOs20VendorDescriptor(MsOsVendorCode, MsOsSet)
};


inline constexpr USBDescriptorSet Descriptors = {
	DeviceDescriptor,
	Configuration,
	Strings,
	Bos,
	VendorRequests
};


static_assert(USBDeviceClassMatchesConfiguration(DeviceDescriptor, Configuration), "device class must match the configuration");
static_assert(USBDeviceSupportsBos(DeviceDescriptor), "a BOS needs bcdUSB 2.01 or later");


inline constexpr uint8 VendorInterfaces[] = {
	0
};


inline constexpr uint8 VendorEndpoints[] = {
	0x81,
	0x02
};


}
```

Notes on the example:

- `Strings` is an array of spans. The set keeps a span to it, so the array must have static storage. `inline constexpr` at namespace scope gives that.
- `DeviceDescriptor` declares bcdUSB 2.10 because it has a BOS. The class is 0 because the configuration has no interface association.
- `MakeUSBConfigurationDescriptor()` sets the remote wakeup bit. The device is bus powered, with 50 units (100 mA).
- `VendorInterfaces` and `VendorEndpoints` are the arrays a function takes through its span constructor. Their values must match the interface and the endpoints above. The fluent form takes the layout `AppUsb::Function(0)` instead, which carries the same values.

The set compiles with `-Wall -Wextra -Werror`, and its size annotations are checked by the compiler. The host tests do not run this set: `.tool/sim/Descriptors.h` holds the simulator's own bytes.

## Rules to Keep

- Use the fluent builder for a device's descriptors. Use the positional builders only for bytes the fluent builder does not write.
- A set's arrays must be `constexpr` or `static`. The device reads them later.
- `bMaxPacketSize0` is 64 only. Any other value fails at compile time.
- The interface and endpoint numbers in the configuration must match the functions. The core routes by them. The layout from `Function(index)` carries them, so a function built from it cannot disagree with the descriptors.
- Give every identity value. The builders have no defaults for VID, PID, bcdDevice, the manufacturer, the product, the serial number or the GUID. A missing one is a compile error.
