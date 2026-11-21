# USB Device Core

The device core owns the control pipe and the standard requests. It routes class, vendor and endpoint traffic to the functions. The adapter reports what the hardware saw. The core decides what it means.

Headers:

- `#include <Drivers/Interface/USB/Device/USBDevice.h>`: `USBDevice`, the core
- `#include <Drivers/Interface/USB/Device/USBFunction.h>`: `USBFunction` and `USBDeviceContext`
- `#include <Drivers/Interface/USB/USBProtocol.h>`: chapter 9 values and `USBSetupPacket`

The core is header-only. It takes the adapter by reference, so it does not depend on a vendor port.

## USBDevice

```cpp
USBDevice(USBDeviceAdapter& adapter, const USBDescriptorSet& descriptors, std::span<USBFunction* const> functions);
```

The device keeps copies of the descriptor spans, not the bytes. The arrays behind the spans, and the functions, must outlive the device. The device cannot be copied.

| Method | Context | Returns | Description |
|--------|---------|---------|-------------|
| `Start(const USBDeviceAdapter::Parameters&)` | Thread | `ResultStatus` | Checks the set and the functions, applies the parameters, subscribes to the adapter events and connects |
| `SetSerialNumber(std::span<const uint8>)` | Thread, before `Start()` | `ResultStatus` | Gives the string at iSerialNumber at run time. Refused unless the set reserves it (`SerialNumberAtRuntime()`). See [Serial Number](#serial-number) |
| `SetRuntimeString(uint8, std::span<const uint8>)` | Thread, before `Start()` | `ResultStatus` | Gives the descriptor of a run-time string the set reserves, such as a MAC address. See [Run-Time Strings](#run-time-strings) |
| `Stop()` | Thread, in a critical section | none | Deconfigures, closes the endpoints and detaches. Does nothing when not started |
| `Process()` | Thread | none | Calls `Process()` of each function |
| `GetState()` | Either | `USBDeviceState` | Current state, see below |
| `GetAddress()` | Either | `uint8` | Device address, 0 to 127 |
| `GetConfigurationValue()` | Either | `uint8` | Selected configuration, 0 when not configured |
| `IsRemoteWakeupEnabled()` | Either | `bool` | Set by the host with `SET_FEATURE(DEVICE_REMOTE_WAKEUP)` |
| `GetErrorCount()` | Either | `uint32` | Errors the adapter reported |
| `RemoteWakeup()` | Either | `ResultStatus` | `notReady` unless suspended, `notSupported` unless the host enabled it |

### Start

`Start()` returns:

| Result | Cause |
|--------|-------|
| `ok` | Started and connected. A board with a fixed pull-up returns `notSupported` from `Connect()`, and `Start()` treats that as `ok` |
| `busy` | Already started |
| `invalidParameter` | A bad set or a null function. See the checks below |
| Any adapter error | Passed through from `SetParameters()` or `Connect()` |

Checks on the set:

- The device descriptor is 18 bytes, its type is 1, and `bMaxPacketSize0` is 64.
- The configuration's type is 2, its `wTotalLength` equals its size, and its `bConfigurationValue` is not 0.
- The BOS, if present, is at least 5 bytes, has type 15, and its `wTotalLength` equals its size.
- Each string has a `bLength` that equals its size and type 3.
- A set with `serialNumberAtRuntime` needs a serial number from `SetSerialNumber()`, and a non-zero iSerialNumber in its device descriptor.
- Every run-time string the set reserves needs a descriptor from `SetRuntimeString()`. A set that reserves more than four is refused.

A refused `Start()` changes nothing, so the same device starts once the missing descriptors are given.

The state is `Detached` until the first bus reset.

### Serial Number

`SetSerialNumber()` gives the device a serial number string that is known only at run time, such as the MCU's unique ID. The set reserves the string with `SerialNumberAtRuntime()` in the [fluent builder](../Descriptor/README.md#runtime-serial-number). Then:

| Result | Cause |
|--------|-------|
| `ok` | The descriptor is kept |
| `busy` | The device is started. The string cannot change while the host may read it |
| `invalidParameter` | Not a string descriptor: fewer than 4 bytes (an empty string), an odd size, `bLength` not equal to the size, or `bDescriptorType` not 3 |

The device keeps the span, not a copy, so the array must have static storage. The helper `MakeUSBSerialNumberDescriptor()` (in `Descriptor/USBSerialNumber.h`) builds the descriptor from an ID's bytes.

GET_DESCRIPTOR answers iSerialNumber with this string, cut to `wLength` like the other strings. The language in `wIndex` is ignored, as for the other strings.

```cpp
static std::array<uint8, 2 + 4 * 12> serialDescriptor{};

// Before the first Start(). The ID is any 12-byte value that is unique per device.
ResultStatus status = MakeUSBSerialNumberDescriptor(uniqueId, serialDescriptor);
if (status == ResultStatus::ok) {
	status = device.SetSerialNumber(serialDescriptor);
}
```

A set that does not reserve the serial number does not need the call. If it is called anyway, the string replaces the one at iSerialNumber.

### Run-Time Strings

A class reserves a run-time string in the [fluent builder](../Descriptor/README.md#run-time-strings) (`RuntimeStringCount()`, `RawWithRuntimeString()`), and the set lists its index in `runtimeStringIndexes`. Then:

| Result | Cause |
|--------|-------|
| `ok` | The descriptor is kept. A later call for the same index, before `Start()`, replaces it |
| `busy` | The device is started |
| `invalidParameter` | The set does not reserve the index, or the descriptor is not a string descriptor: `bLength` not equal to the size, `bDescriptorType` not 3, an odd size, or fewer than 4 bytes (an empty string) |

The device keeps the span, not a copy, so the array must have static storage, and it must stay valid for the life of the device. The four slots are fixed, one for each run-time string a configuration can reserve. A slot is kept for the index of the same position in the set's list.

GET_DESCRIPTOR(STRING) answers a run-time index with its descriptor, cut to `wLength`. The language in `wIndex` is ignored, as for the other strings.

```cpp
// Before the first Start(). The MAC address is the same in every boot, so the host finds the same interface again.
ResultStatus SetMacString(USBDevice& device) {
	ResultStatus status = MakeUSBHexStringDescriptor(macAddress, macDescriptor);
	if (status != ResultStatus::ok) {
		return status;
	}

	return device.SetRuntimeString(AppUsb::RuntimeStringIndexes[0], macDescriptor);
}
```

The function that reserved the string learns its index from its layout (`USBFunctionLayout::runtimeStrings`), as the layout is given to the function's constructor.

### Stop and Process

`Stop()` calls `OnDeconfigured()` of the functions when configured, closes all endpoints except endpoint 0, disconnects, and returns the state to `Detached` with address and configuration 0. The core does not enter a critical section. The caller does:

```cpp
System::CriticalSection(true);
device.Stop();
System::CriticalSection(false);
```

`Process()` runs in thread context only. Each function does its deferred work there.

## States

| State | Entered by | Left by |
|-------|------------|---------|
| `Detached` | `Start()` before the first bus reset, `Stop()` | Bus reset |
| `Default` | Bus reset (address 0, configuration 0), SET_ADDRESS to 0 | SET_ADDRESS to a non-zero address (after its status stage), suspend |
| `Address` | SET_ADDRESS completes with a non-zero address, or SET_CONFIGURATION(0) | SET_CONFIGURATION, bus reset, suspend |
| `Configured` | SET_CONFIGURATION with a valid value | SET_CONFIGURATION(0), bus reset, suspend |
| `Suspended` | Suspend from `Default`, `Address` or `Configured` | Resume, which returns to the state before suspend |

The address is pending between SET_ADDRESS and its status stage. The state changes only when the status stage completes. A new SETUP drops a pending address.

## Control Stages

Every SETUP is answered by exactly one of: a data stage, a status stage, or a stall on `0x80`.

| Stage | Meaning |
|-------|---------|
| `Idle` | No request in progress |
| `DataIn` | The device sends the data stage. The host reads the bytes |
| `StatusOut` | The host sends the zero-length status packet after an IN data stage |
| `DataOut` | The host sends the data stage. The device receives it |
| `StatusIn` | The device sends the zero-length status packet after an OUT data stage or a request with no data |

Rules:

- A new SETUP at any stage abandons the request and starts again at `Idle`. The adapter has already dropped the transfer in flight.
- For an IN data stage, the core sends `min(data, wLength)` bytes. A zero-length packet ends a short answer that fills whole packets. An answer of exactly `wLength` bytes gets no zero-length packet.
- For an OUT data stage, the core receives exactly `wLength` bytes into the buffer the function gives. `OnControlDataReceived()` then decides the request.
- A request whose function returns `ok` without starting a data stage gets a status stage if `wLength` is 0. If `wLength` is above 0, the core stalls it.
- A `DataOverrun` during an OUT data stage stalls the request.

## Bus Reset, Suspend and Resume

| Event | Core action |
|-------|-------------|
| Bus reset | Closes all endpoints except 0. Calls `OnBusReset()` of each function. State `Default`, address 0, configuration 0, remote wakeup off. Does not call `OnDeconfigured()` |
| Suspend | State `Suspended`, then `OnSuspend()` of each function |
| Resume | Back to the state before suspend, then `OnResume()` of each function |

The adapter raises its resume event only after the resume signalling ends. `RemoteWakeup()` returns before that.

## Standard Requests

The core answers these itself:

| Request | Recipient | Behaviour |
|---------|-----------|-----------|
| GET_STATUS | Device | Two bytes. Bit 0 is self-powered (from the configuration), bit 1 is remote wakeup enabled |
| GET_STATUS | Endpoint | Two bytes. Bit 0 is the halt. Endpoints 0x00 and 0x80, and owned endpoints. Other addresses are stalled |
| GET_STATUS | Interface | Two zero bytes. Owned interfaces only |
| SET_ADDRESS | Device | Address 0 to 127, state `Default` or `Address`, `wIndex` and `wLength` 0 |
| GET_DESCRIPTOR | Device | Device descriptor |
| GET_DESCRIPTOR | Device | Configuration with index 0, the only one |
| GET_DESCRIPTOR | Device | String with the index in `wValue`. `wIndex` (language) is ignored |
| GET_DESCRIPTOR | Device | BOS, when the set has one |
| SET_CONFIGURATION | Device | In `Address` or `Configured`. Value 0 deconfigures. Calls `OnConfigured()` of each function in order |
| GET_CONFIGURATION | Device | One byte, in `Address` or `Configured` |
| SET_FEATURE, CLEAR_FEATURE | Device | Only `DEVICE_REMOTE_WAKEUP` |
| SET_FEATURE, CLEAR_FEATURE | Endpoint | `ENDPOINT_HALT` on an owned endpoint. Endpoint 0 is acknowledged with no effect |
| GET_INTERFACE | Interface | In `Configured`, owned interfaces, from `GetInterface()` |
| SET_INTERFACE | Interface | In `Configured`, owned interfaces, calls `OnSetInterface()` |

GET_DESCRIPTOR answers are cut to `wLength`. The device qualifier (type 6) and the other-speed configuration (type 7) are stalled, because the device has one speed.

Requests whose direction is wrong for the standard request are stalled. Requests to an interface or to an endpoint other than 0 are stalled unless the device is configured. A suspended device counts by the state it was in before the suspend.

### Stalled requests

The core stalls (`Stall(0x80)`) when:

- a standard request is unknown, or is SYNCH_FRAME or SET_DESCRIPTOR
- a request has a reserved type or a reserved recipient
- a GET_DESCRIPTOR asks for a type not in the set, or an index that does not exist
- a request names an interface or endpoint that no function owns
- a SET_CONFIGURATION names another value, or a function fails in `OnConfigured()`
- a function returns anything but `ok` from `OnControlRequest()`

### Vendor requests to the device

A vendor request to the device with direction IN is answered from `USBDescriptorSet::vendorRequests`. The entry must match `bRequest` and `wIndex`. Any other vendor request to the device is stalled. The core has no built-in vendor request.

## Class and Vendor Requests

A class or vendor request whose recipient is an interface or endpoint goes to the function that owns it. The function's `OnControlRequest()` decides it:

- `ok`: the function started a data stage with `ControlSendData()` or `ControlReceiveData()`, or the request has no data and gets a status stage.
- Anything else: the core stalls the request.

## USBFunction

A function is a class with the virtual methods below. A composite configuration holds several functions. Each owns its interfaces and endpoints.

```cpp
class VersionFunction : public USBFunction {
public:
	VersionFunction(std::span<const uint8> interfaces, std::span<const uint8> endpoints)
		: USBFunction(interfaces, endpoints) {
	}


	ResultStatus OnConfigured(USBDeviceContext& context) override {
		(void)context;
		return ResultStatus::ok;
	}


	ResultStatus OnControlRequest(const USBSetupPacket& setup, USBDeviceContext& context) override {
		if (setup.type != USBRequestType::Vendor || setup.direction != USBDirection::In || setup.request != 0x10) {
			return ResultStatus::notSupported;
		}

		return context.ControlSendData(version);
	}


private:
	static constexpr std::array<uint8, 2> version = { 1, 0 };
};
```

`ControlSendData()` keeps the span. The span must stay valid until the stage completes. A static member, as above, meets that.

| Method | Default | Called |
|--------|---------|--------|
| `OnConfigured(USBDeviceContext&)` | Pure virtual. Opens the endpoints. Anything but `ok` deconfigures and stalls | Interrupt, from SET_CONFIGURATION |
| `OnDeconfigured()` | Nothing | Interrupt, before endpoints close |
| `OnBusReset()` | Nothing | Interrupt |
| `OnSuspend()`, `OnResume()` | Nothing | Interrupt |
| `OnControlRequest(setup, context)` | `notSupported` | Interrupt, for an owned interface or endpoint request |
| `OnControlDataReceived(setup, data, context)` | `notSupported` | Interrupt, when an OUT data stage has arrived. `ok` acks, anything else stalls |
| `OnTransferComplete(address, byteCount, context)` | Nothing | Interrupt, for an owned endpoint |
| `OnEndpointHaltChanged(address, halted, context)` | Nothing | Interrupt, for an owned non-control endpoint after a successful halt or clear |
| `OnSetInterface(interface, alternate, context)` | Accepts alternate 0 only | Interrupt |
| `GetInterface(interface)` | 0 | Interrupt |
| `Process(context)` | Nothing | Thread, from `USBDevice::Process()` |

`OnEndpointHaltChanged()` tells the function that a stalled endpoint lost its transfer. A stall abandons the transfer without an event.

## USBDeviceContext

The core passes this to every callback that may use it. A function keeps no pointer to it.

| Method | Use |
|--------|-----|
| `OpenEndpoint(config)`, `CloseEndpoint(address)` | Endpoints, forwarded to the adapter |
| `StartInTransfer(address, data, zeroLengthPacket)` | Starts an IN transfer |
| `StartOutTransfer(address, buffer)` | Arms an OUT transfer |
| `Stall(address)`, `ClearStall(address)` | Halts or clears an endpoint. A successful call on a non-control endpoint runs `OnEndpointHaltChanged()` of its owner |
| `RemoteWakeup()` | Same as `USBDevice::RemoteWakeup()` |
| `ControlSendData(data)` | Data stage of the request being answered. Only inside `OnControlRequest()` |
| `ControlReceiveData(buffer)` | OUT data stage of the request being answered. The buffer must hold `wLength` bytes |

`ControlSendData()` returns `notReady` outside a request. It returns `invalidParameter` for a request in the wrong direction or with `wLength` 0, and `busy` when a data stage is already started.

From an interrupt callback, these calls need no critical section. From `Process()`, a call that starts or stops a transfer can overlap the interrupt for the same endpoint. Wrap such a call in `System::CriticalSection(true)` and `(false)`.

## Errors

`GetErrorCount()` counts every error the adapter reports. A `DataOverrun` in an OUT data stage also stalls that request. The other errors are only counted.

## Limits

- One configuration, one speed.
- Four run-time strings in one configuration (`USBMaxRuntimeStrings`), each given before `Start()` in static storage.
- Critical sections nest, so a caller may wrap a call in its own section, even inside another one. The core opens no section of its own.
- No allocation. The core stores what it needs in its own members.
- A request with a data stage must have one. The core stalls a request that says it has data and gets none.
