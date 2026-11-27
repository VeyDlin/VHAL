# HID Class

A Human Interface Device function: an interface with an interrupt IN endpoint, and optionally an interrupt OUT endpoint, that carries reports to and from a host. Keyboards, mice and a vendor-defined raw channel use it. A keyboard or a mouse is taken by the host's in-box HID driver, so it needs no driver of its own. The vendor raw report is reached through the system's HID API. The stack is tested in the host simulator and on the USBComposite demo: on Windows 11 both HID interfaces start with the zero-size-free report descriptors described below (see [Host Support](../../README.md#host-support)).

Headers:

- `#include <Drivers/Interface/USB/Class/HID/USBHidReportBuilder.h>`: report descriptors, the presets and `USBHidReportDescriptor`
- `#include <Drivers/Interface/USB/Class/HID/USBHidBuilder.h>`: the interface, for the configuration builder
- `#include <Drivers/Interface/USB/Class/HID/HidFunction.h>`: the function, the class requests and the report queue
- `#include <Drivers/Interface/USB/Class/HID/HidKeyboard.h>` and `HidMouse.h`: the keyboard and the mouse on top of the function

## Report Descriptors

`USBHidReportBuilder` writes a report descriptor at compile time with the same technique as the device descriptors (see [Descriptor](../../Descriptor/README.md)). The chain is written in a captureless lambda. `USBHidReportDescriptor` gives the bytes and the report table of the built chain:

```cpp
using KeyboardReport = USBHidReportDescriptor<+[] { return USBHidReportBuilder::BootKeyboard(); }>;

// KeyboardReport::Bytes is the descriptor (64 bytes). KeyboardReport::Info is what the class builder and the function take.
```

The chain has one item per method: `UsagePage`, `Usage`, `UsageMinimum`, `UsageMaximum`, `Collection(USBHidCollection)`, `EndCollection`, `LogicalMinimum`, `LogicalMaximum`, `PhysicalMinimum`, `PhysicalMaximum`, `Unit`, `UnitExponent`, `ReportSize`, `ReportCount`, `ReportId`, and `Input`, `Output`, `Feature` with the flags of `USBHidFlags` (`Constant`, `Variable`, `Relative`, `Wrap`, `NonLinear`, `NoPreferred`, `NullState`, `Volatile`, `BufferedBytes`; the zero value of each pair is the first name, `Data`, `Array` and `Absolute`).

Items use the shortest encoding of HID 1.11 section 6.2.2.2, with one rule on top: every item with a value takes at least one data byte, so a value of 0 is one byte of 0, and logical and physical ranges are signed. So `LogicalMinimum(0)` is `0x15 0x00`, `UsageMinimum(0)` is `0x19 0x00`, an `Input(Array)` is `0x81 0x00`, and a physical collection is `0xA1 0x00`. Only `EndCollection` takes no data.

The zero-size form (bSize 0, the value 0 with no data byte) is allowed by HID 1.11, but Windows' HID parser rejects it. On the first hardware run (Windows 11, the USBComposite demo), both HID interfaces failed to start: the keyboard with 0xC000001D and the mouse with 0xC00000B9 (STATUS_COULD_NOT_INTERPRET). The builder wrote those items with no data byte. With every value given one data byte, both devices started and the composite HID checks passed. `TestHidDescriptors.cpp` checks that no preset has a zero-size item other than End Collection (`CheckNoZeroSizeItems`). Push and Pop would also be exempt, but the builder does not write them.

| Preset | Input | Output | Notes |
|--------|-------|--------|-------|
| `BootKeyboard()` | 8 bytes: modifiers, reserved, six keys | 1 byte: five LEDs and padding | HID 1.11 Appendix B.1, with the key array widened to usages and logical values 0 to 255 as most keyboards send it. No report ID. 64 bytes. A boot protocol host reads the fixed 8 bytes and ignores the descriptor |
| `BootMouse()` | 4 bytes: buttons (3 and padding), X, Y, wheel | none | HID 1.11 Appendix B.2 with the wheel appended. The first 3 bytes are the boot report. No report ID. 56 bytes |
| `ConsumerControl()` | 2 bytes: one 16-bit usage (0x0000 to 0x03FF) | none | Usage page 0x0C. No report ID. 23 bytes |
| `VendorRaw(n)` | n bytes (1 to 64) | n bytes | Usage page 0xFF00, usage 0x01 in and 0x02 out. No report ID. Any OS opens it without a driver |

Rules. A violated rule is a compile error with its message:

- A collection is closed before the end of the descriptor: `a collection is not closed`, and `EndCollection without a Collection` for an extra one.
- Report ID 0 is reserved: `report ID 0 is reserved`.
- A descriptor uses report IDs in every report or in none: `reports with and without IDs cannot be mixed in one descriptor`.
- A report's bits are a whole number of bytes: `a report is not a whole number of bytes`.
- A report is at most 64 bytes, one full-speed packet, the ID byte included: `a report is at most 64 bytes, one full-speed packet`.
- `ReportSize` and `ReportCount` are set before a main item: `ReportSize and ReportCount are set before an Input, Output or Feature item`.
- `UnitExponent` is -8 to 7, and a descriptor has at most 16 distinct reports, each one a type and an ID.

`Info` holds the lengths per report type and ID, with the ID byte counted. `InputLength(id)`, `OutputLength(id)`, `FeatureLength(id)`, `MaxLength(type)` and `UsesReportIds()` are what the function checks `SendReport()` and `SET_REPORT` lengths against.

## Class Builder

`USBHidBuilder` plugs into `USBConfigurationBuilder::Function()` (see [Descriptor](../../Descriptor/README.md#class-builders)). It writes one interface of class 0x03 and the HID descriptor between the interface and its endpoints. The HID descriptor is `bcdHID` 0x0111, country code 0 and one report descriptor, whose length is the length of `Info.descriptor`.

| Method | Description |
|--------|-------------|
| `BootKeyboard()` | Subclass 1, protocol 1. The report must have an 8-byte input without a report ID |
| `BootMouse()` | Subclass 1, protocol 2. The report must have a 3-byte input without a report ID |
| `Report(info)` | The report descriptor. Required |
| `In(address)` | The IN endpoint address. Without it the configuration builder assigns the number |
| `OutEndpoint()` | An interrupt OUT endpoint, with an address the configuration builder assigns |
| `Out(address)` | An interrupt OUT endpoint with the given address |
| `IntervalMs(n)` | Polling interval of both endpoints, 1 to 255 ms. Default 1 |
| `PacketSize(n)` | Packet size of both endpoints, 1 to 64. Default: the largest report of each direction, ID byte included |
| `Name(text)` | The interface string |

Without `BootKeyboard()` or `BootMouse()` the interface has subclass 0 and protocol 0. The IN endpoint comes first in the layout, then the OUT endpoint.

```cpp
using AppUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		// ... identities ...
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBHidBuilder().BootKeyboard().Report(KeyboardReport::Info).In(0x81)));
}>;
```

A packet size that cannot hold the largest report is a compile error (`the packet size holds the largest input report`). So is a boot device whose report has no input of 8 bytes (keyboard) or 3 bytes (mouse) without a report ID. The check looks at the length only, so the report's items are the application's to get right.

The report descriptor bytes do not go into the configuration. The configuration holds only the HID descriptor, which carries the length. The function gets the bytes from the same `KeyboardReport::Info` object that `Report()` took, so the bytes the host reads and the bytes the function answers with come from one object. `OnConfigured()` refuses the function when the HID descriptor's length differs from the report, or when the layout's endpoints do not match one IN and at most one OUT endpoint.

## Function

`HidFunction<QueueDepth>` takes the layout of the function and the report information:

```cpp
HidFunction<8> keyboardFunction(AppUsb::Function(0), KeyboardReport::Info);
```

It answers the standard and class requests of HID 1.11 chapter 7 for the interface:

| Request | Answer |
|---------|--------|
| GET_DESCRIPTOR HID (0x21) | The 9-byte HID descriptor from the layout |
| GET_DESCRIPTOR Report (0x22) | The report descriptor. A shorter `wLength` gets the first bytes |
| GET_REPORT input, output | The last report of that ID, zeros before any |
| GET_REPORT feature | The feature delegate fills the report |
| SET_REPORT output | The output delegate. A length other than the output report's stalls |
| SET_REPORT feature | The feature delegate. It returns false to stall |
| SET_REPORT input | Kept as the last input report of that ID |
| GET_IDLE, SET_IDLE | One period per report ID (0 for all). A period is in 4 ms units |
| GET_PROTOCOL, SET_PROTOCOL | Only on a boot subclass interface. 0 is boot, 1 is report. Other devices stall |

Each request returns a status. A request the function does not answer, or answers with a status other than `ok`, is stalled by the core.

Input reports:

| Method | Thread context | Description |
|--------|----------------|-------------|
| `SendReport(span)` | Yes | Queues one input report. The span is the whole report, starting with the ID byte when the descriptor uses IDs. It is copied. `invalidParameter` for a wrong length or ID, `notReady` before configuration, `busy` when the queue is full |
| `SendReport(span, steady)` | Yes | The same, and `steady` is the report that GET_REPORT and the idle repeat answer with. The mouse uses it so that a repeat carries the buttons and no motion. It has the length and the ID of the report |
| `FreeSlots()` | Yes | Free places in the queue. A report in flight keeps its place until the host has taken it |
| `Process(context)` | Yes | Starts the head of the queue and runs the idle repeats. `device.Process()` calls it |

- `SendReport()` only queues. `Process()` and the completion of the IN transfer send the queue, one report at a time, in order.
- The function keeps state for at most four distinct report IDs (of all types together). `OnConfigured()` refuses a descriptor with more.
- Idle: a non-zero period makes `Process()` queue a copy of the last input report of that ID each period, and the period is a minimum: nothing is repeated before it has passed. It queues nothing while a report of that ID is queued. The copy is the steady report of `SendReport()`, so a relative device repeats no motion. A zero period sends only what the application sends.
- Boot protocol sends the first 8 bytes (keyboard) or 3 bytes (mouse) of a report. The cut is made when a report is sent, so a switch of protocol applies to the reports already queued.
- Remote wakeup: the configuration must advertise it (`RemoteWakeup()` in `USBConfigurationBuilder`), or the host never enables it; `USBHidBuilder` does not set it. A report that waits while the device is suspended asks for resume through the context, once per suspension. When the host has not enabled remote wakeup, the request is refused and the report waits for the host's own resume.
- A halt of the IN endpoint keeps the head queued, and it is sent after the host clears the halt.
- A bus reset and a new configuration drop the queue. Deconfiguration (SET_CONFIGURATION 0, or `Stop()`) drops it too. Both then call the reset delegate, so the application's state that describes what the host has seen is dropped as well: `HidMouse` drops its pending movement this way.

Output reports arrive from the OUT endpoint (a packet of another length than the output report is dropped) or from SET_REPORT(Output). Both call the output delegate in interrupt context. The OUT endpoint is armed with one packet, so a report of the packet size completes it.

| Delegate | Signature | Called for |
|----------|-----------|------------|
| `OutputDelegate` | `void(span report)` | Output reports, from the OUT endpoint or SET_REPORT |
| `FeatureGetDelegate` | `bool(reportId, span report)` | GET_REPORT feature. It fills the report, which is exactly the feature length |
| `FeatureSetDelegate` | `bool(span report)` | SET_REPORT feature. It returns false to stall |
| `ResetDelegate` | `void()` | The function is deconfigured or the bus resets, after the queue is cleared. It runs in the context of the event (interrupt, or the section of `Stop()`), so it only touches state that the thread side guards with a section |

The delegates are `Callback::Delegate`s, so they never allocate. Set them with `SetOutputDelegate()`, `SetFeatureGetDelegate()`, `SetFeatureSetDelegate()` and `SetResetDelegate()`. Setting a delegate returns `busy` while the function is configured. Clearing one (passing an empty delegate) is accepted at any time, inside the function's section, so the owner of a delegate can take it back when it is destroyed while the device runs. Once cleared, the delegate is not called.

## Keyboard

`HidKeyboard<QueueDepth>` keeps the modifiers and the held keys, and sends a full boot report with each change. A report that waits in the queue is therefore never out of date when it is sent.

```cpp
HidKeyboard<8> keyboard(keyboardFunction);
keyboard.TypeText("Hello");
```

| Method | Description |
|--------|-------------|
| `Press(keyCode)`, `Release(keyCode)` | One report. A key code of 0xE0 to 0xE7 is a modifier. Key codes 0x04 to 0xDD are keys (the keyboard page, so F13 is 0x68 and F24 is 0x73); 0x00 to 0x03 (the error codes), 0xDE, 0xDF and 0xE8 and above are `invalidParameter`. Repeating a state is a no-op |
| `ReleaseAll()` | Releases every key and modifier with one report |
| `SetModifiers(mask)` | Sets the modifier byte, one report |
| `Tap(keyCode, modifiers)` | Press, then release: two reports. The press carries the held modifiers plus `modifiers`; the release carries the held modifiers. A key that is held is refused |
| `TypeText(text)` | Types characters on the US layout, two reports each. Returns how many it typed. Each character is pressed with exactly the modifiers its key needs (Shift or none), so held Ctrl types the plain key. Stops at a character with no key, at a character whose key is held, and at a character that does not fit the queue |
| `Modifiers()`, `Leds()` | The current modifier byte and the LED bits the host last sent |
| `SetLedDelegate(delegate)` | Runs in interrupt context when the LED bits change |

- Up to six keys are reported. A seventh held key makes the report ErrorRollOver (every key slot 0x01) until a key is released. Up to 32 keys can be held; the 33rd is `outOfMemory`.
- A call the queue refuses (`busy`) leaves the state as it was. `Tap()` needs two free places and checks them first.
- `USBHidAsciiToKey(char, stroke)` maps a character to its key code and Shift state. Tab and Enter are typed. A carriage return has no key.
- The keyboard sets itself as the function's output delegate, so its LED handling runs for both paths. Create the keyboard before the device starts, and keep it alive as long as the function is: the constructor cannot report a refusal, and after the device is configured the registration is refused, so LEDs would not be delivered.

## Mouse

`HidMouse<QueueDepth>` sends buttons, X, Y and the wheel, four bytes per report.

| Method | Description |
|--------|-------------|
| `Move(dx, dy)`, `Scroll(wheel)` | Adds to the pending movement and sends what the queue has room for |
| `Flush()` | Sends the pending movement that the queue has room for. Call it from the loop after a `busy` |
| `SetButtons(mask)` | Sets the buttons (`ButtonLeft`, `ButtonRight`, `ButtonMiddle`) and sends one report at least |
| `Click(button)` | Press and release: two reports, after the pending movement. Refused with nothing sent unless there are places for the pending movement and then two more |
| `Buttons()` | The buttons that are down |

- A movement of more than 127 steps per axis is split into reports of at most 127 each, one per queue place.
- While the queue is full, the movement waits in the mouse and the call returns `busy`. `Flush()` sends it later.
- The pending movement of each axis, the wheel included, is bounded to `MaxPendingSteps` (32767, the int16 range). A `Move()` or `Scroll()` that would take an axis past the bound returns `outOfRange` and changes nothing; the host has to take the pending reports first. `busy` means the movement was taken and waits for queue room, so the two answers tell a caller whether the movement is pending. The sums are taken in 64 bits, so no argument overflows.
- The pending movement is dropped when the function is deconfigured or the bus resets (the mouse registers for the function's reset delegate). The host does not receive movement that was made while it was away as one jump after the next enumeration. The buttons are not reset by this; the application sets them.
- `SetButtons()` keeps the new buttons when its report is refused, so the next report carries them.
- The idle repeat and GET_REPORT of the mouse answer with the buttons and no motion, so a repeat does not move the pointer again.

## Interrupt Context and Critical Sections

| Code | Context | Rule |
|------|---------|------|
| `OnConfigured`, `OnDeconfigured`, `OnBusReset`, `OnSuspend`, `OnResume`, `OnControlRequest`, `OnControlDataReceived`, `OnTransferComplete`, `OnEndpointHaltChanged` | Interrupt | Short. No section is entered, because `OnDeconfigured` also runs inside `Stop()`, which the caller wraps in a section |
| Output, feature and reset delegates, `SetLedDelegate` callback | Interrupt | Short. Keep it simple and bounded |
| `SendReport()`, `FreeSlots()`, `Process()` | Thread | Shares the queue with the interrupt through short sections. The transfer start is inside the section too: sections nest, and a completion inside the call runs in the same section |
| `HidMouse` `Move()`, `Scroll()`, `Flush()`, `SetButtons()`, `Click()` | Thread | Each takes a section around the pending movement, since the reset delegate clears it from the interrupt |
| `SetOutputDelegate()` and the other setters | Thread | Setting: `busy` while configured. Clearing: any time. Each setter takes a section |

## Tests

The host tests are in `.test` and run with `./test.cmd USB`:

- `TestHidDescriptors.cpp`: the presets, the HID descriptor, the configurations of the boot keyboard, the boot mouse, the vendor device and the identified report, checked against arrays written out by hand from HID 1.11, and no zero-size item except End Collection. The builder is not used to produce them.
- `TestHid.cpp`: the class requests, the queue, the idle repeat, protocol switching, remote wakeup, halts, bus reset and both output paths, through the simulated host.
- `TestHidInput.cpp`: the keyboard and the mouse byte for byte, including the key code range (0x04 to 0xDD), F13 and F24, the mouse's pending bound and the drop of the pending movement on deconfiguration and bus reset.
- `TestHidInput.cpp`: the keyboard (key state, rollover, `Tap`, `TypeText`, the key limit, LEDs) and the mouse (splitting, accumulation, buttons and clicks), byte for byte.
- `BuilderErrors/hid_*.cpp`: the compile errors above, each with the message it must give.
