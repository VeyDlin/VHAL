# USBDeviceAdapter

The device-side USB controller interface. It describes what the hardware does: endpoints, transfers, the pull-up, the address and the interrupt. It knows nothing about USB requests. The [device core](../../Common/Drivers/Interface/USB/Device/README.md) reads the packets the adapter reports and decides what they mean.

Header: `#include <Adapter/USBDeviceAdapter.h>`

The class is not a template, like [SocketAdapter](SocketAdapter.md). The core takes it by reference and never sees the vendor handle. A port derives from it and keeps the handle. The STM32G4 port is [USBDeviceAdapterG4](Port/STM32/G4/USBDeviceAdapterG4.md).

Endpoint addresses are USB addresses. `0x81` is endpoint 1 IN. `0x01` is endpoint 1 OUT.

## Shared Types

The adapter and the core share the types in `Adapter/Utilities/USBTypes.h`. They hold no heap memory and can be passed by value from an interrupt handler.

| Type | Values or members | Description |
|------|-------------------|-------------|
| `USBEndpointType` | `Control`, `Isochronous`, `Bulk`, `Interrupt` | Transfer type. Isochronous is not used by the stack |
| `USBSpeed` | `Low`, `Full`, `High` | Full speed is the only speed of the current ports. High is reserved for a port with an external PHY |
| `USBSetupBytes` | `bytes[8]` | The 8-byte SETUP packet as received. `USBSetupPacket::Parse()` decodes it |
| `USBEndpoint` | `Number()`, `IsIn()`, `IsControl()`, `IsValid()`, `MakeIn()`, `MakeOut()`, `ControlOut` (`0x00`), `ControlIn` (`0x80`) | Address helpers. Bit 7 is the direction. Bits 3 to 0 are the number. Bits 6 to 4 must be 0 |

`IsControl()` is true for `0x00` and `0x80` only. `IsValid()` checks bits 6 to 4.

## Parameters

```cpp
struct Parameters {
    uint8 endpointCount = 4;
    bool startOfFrameEvents = false;
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `endpointCount` | 4 | Endpoint numbers in use, endpoint 0 included. Numbers 0 to `endpointCount - 1` may be opened. `SetParameters()` returns `invalidParameter` for 0 or for more than the hardware has (8 on the G4) |
| `startOfFrameEvents` | `false` | Raises `onStartOfFrame` every 1 ms. Off by default, because the interrupt costs time on every frame |

## EndpointConfig

```cpp
struct EndpointConfig {
    uint8 address = 0;
    USBEndpointType type = USBEndpointType::Bulk;
    uint16 maxPacketSize = 64;
    bool doubleBuffer = false;
};
```

| Field | Description |
|-------|-------------|
| `address` | Endpoint address. Endpoint 0 is refused |
| `type` | `Bulk` or `Interrupt`. Control is refused |
| `maxPacketSize` | Full speed: 8, 16, 32 or 64 for bulk, 1 to 64 for interrupt |
| `doubleBuffer` | Bulk only. Two packet buffers, so the hardware keeps one busy while the other is handled. A port that cannot give it returns `notSupported`, for example fsdev with the other direction of the number open. Any other type with this set returns `invalidParameter` |

## Error

| Value | Meaning |
|-------|---------|
| `DataOverrun` | A packet did not fit the room in the armed OUT buffer. It was not stored |
| `Crc`, `BitStuffing`, `Timeout`, `Framing` | Bus errors, reported only when the port sees them |
| `Unknown` | Any other error the port reports |

The core counts every error. It acts on `DataOverrun` only.

## Events

Assign them before `Connect()`. They run in interrupt context, so a handler must be short. It must not block or allocate. A null function is skipped.

| Event | Arguments | When |
|-------|-----------|------|
| `onBusReset` | `USBSpeed` | After the adapter has opened endpoint 0 again and set the address to 0 |
| `onSetup` | `const USBSetupBytes&` | For each SETUP packet on endpoint 0 |
| `onTransferComplete` | `uint8 address`, `uint32 byteCount` | When a transfer has finished. `byteCount` is the bytes sent (IN), stored (OUT) or 0 for a zero-length packet |
| `onSuspend`, `onResume` | none | Suspend, and the end of resume signalling |
| `onStartOfFrame` | `uint16 frameNumber` | Each frame, when `startOfFrameEvents` is on |
| `onError` | `Error` | A hardware error. Fires before the transfer's completion, if there is one |

The core subscribes to the first six. The application may use `onStartOfFrame` only.

When `onTransferComplete` runs, the transfer is no longer in flight. The callback may arm the next transfer in the same direction.

## API

| Method | Return | Context | Description |
|--------|--------|---------|-------------|
| `SetParameters(Parameters)` | `ResultStatus` | Thread | Stores the parameters and initializes the controller. `busy` while connected |
| `GetParameters()` | `const Parameters&` | Either | Current parameters |
| `Connect()` | `ResultStatus` | Thread | Sets the D+ pull-up. `notReady` before `SetParameters()` succeeded. `notSupported` when the board has no software pull-up (it is attached from power-up). The core treats `notSupported` as success |
| `Disconnect()` | `ResultStatus` | Thread | Removes the pull-up. Transfers in flight are dropped without events. Endpoints stay open |
| `IsConnected()` | `bool` | Either | Pull-up is on |
| `SetAddress(uint8)` | `ResultStatus` | Either | Stores the address as pending. `invalidParameter` above 127. Takes effect at the status stage, see below |
| `OpenEndpoint(config)` | `ResultStatus` | Either | Opens or reconfigures an endpoint. See the table below |
| `CloseEndpoint(address)` | `ResultStatus` | Either | Closes and frees the endpoint. `invalidParameter` for endpoint 0. `notExist` if not open |
| `CloseAllEndpoints()` | `void` | Either | Closes all endpoints except 0. Used on bus reset and on configuration 0 |
| `StartInTransfer(address, data, zeroLengthPacket)` | `ResultStatus` | Either | Sends `data` on an IN endpoint, see below |
| `StartOutTransfer(address, buffer)` | `ResultStatus` | Either | Arms reception into `buffer` on an OUT endpoint, see below |
| `Stall(address)` | `ResultStatus` | Either | Halts the endpoint. Endpoint 0 halts both directions. `notExist` if not open |
| `ClearStall(address)` | `ResultStatus` | Either | Clears the halt and resets the data toggle to DATA0. See the exception below |
| `IsStalled(address)` | `bool` | Either | The endpoint is halted |
| `RemoteWakeup()` | `ResultStatus` | Either | Starts resume signalling. `notReady` unless suspended. Returns at once. The port raises `onResume` when the signalling ends. See the 5 ms rule below |
| `GetFrameNumber()` | `uint16` | Either | Frame number of the last start-of-frame, 11 bits |
| `IrqHandler()` | `void` | Interrupt | Serves the controller interrupt. The application calls it from its vector |

### OpenEndpoint

| Result | Cause |
|--------|-------|
| `ok` | Opened |
| `invalidParameter` | Address is not a number the `endpointCount` allows, or the type, packet size or `doubleBuffer` does not fit. On a port that shares a number's register between its directions (fsdev), also a type mismatch with the other direction, or a single-buffered request beside a double-buffered other direction |
| `busy` | A transfer is in flight on the address |
| `notSupported` | `doubleBuffer` is asked for and the port cannot give it, for example fsdev with the other direction of the number open |
| `outOfMemory` | Packet memory (fsdev) or FIFO space (dwc2) does not fit the request |

An address already open is reconfigured. The adapter keeps the memory until `CloseEndpoint()` or `CloseAllEndpoints()`.

### StartInTransfer

- The adapter splits `data` into packets of the endpoint's maximum packet size.
- A zero-length packet follows only when `data` is a whole number of packets and `zeroLengthPacket` is set.
- An empty span always sends one zero-length packet.
- `onTransferComplete(address, data.size())` fires when the host has taken the last packet.
- The span must stay valid until then.
- One transfer per direction. `busy` while another is in flight.
- `invalidParameter` for an OUT address. `notExist` for a closed endpoint. `notReady` while disconnected.
- The completion never runs inside this call. It follows the host's IN token, in interrupt context, so `onTransferComplete` runs after `StartInTransfer()` has returned. Unlike `StartOutTransfer()`, this call never completes synchronously.
- From thread context, wrap the call in `System::CriticalSection()`. The interrupt can complete the endpoint while the call is still arming it (fsdev), and the section makes the completion wait until the call has returned.

### StartOutTransfer

- Arms reception into `buffer`. The transfer completes when the buffer is full, or with a short packet. `onTransferComplete(address, stored)` fires with the bytes stored.
- An empty buffer accepts one zero-length packet and completes with 0.
- A packet that does not fit the room left is not stored, not even in part. `onError(DataOverrun)` fires, then the transfer completes with the bytes stored before that packet. The overflowing packet is discarded whole.
- While no transfer is armed, an OUT endpoint NAKs and keeps the host's packet. An IN endpoint NAKs. Neither drops data.
- `invalidParameter` for an IN address. The same return values as `StartInTransfer()` otherwise.
- The call may complete synchronously. A packet that arrived while nothing was armed is delivered from inside the call, so `onTransferComplete` runs before it returns, and the transfer is no longer in flight by then. The call still returns `ok` for the transfer it accepted. A failure status means nothing was armed and no callback follows.
- The caller's critical section covers the synchronous callback. The core's callback stores the bytes at once, inside that section: its ring store opens a section of its own, which nests in the caller's.

### ClearStall

- Clears the halt and resets the data toggle to DATA0. `notExist` for a closed endpoint.
- The port takes no critical section. A thread-context caller wraps the call in `System::CriticalSection()`.
- Exception to the IN contract: on a double-buffered IN endpoint (fsdev), a transfer the hardware had already finished can complete inside this call. `onTransferComplete` then runs before `ClearStall()` returns, and the function may start its next IN transfer there.
- The core calls the function's `OnEndpointHaltChanged()` only after `ClearStall()` returns. A function must not restart its IN while the halt is still set. `VendorBulkFunctionBase` keeps that rule.

### RemoteWakeup

- `notReady` unless the bus is suspended. Returns at once. The port raises `onResume` when the resume signalling ends.
- USB 2.0 section 7.1.7.7 keeps the device suspended for 5 ms before it drives resume. The core does not time that wait, so the caller calls `RemoteWakeup()` at least 5 ms after the suspend. A call from `OnSuspend()` is too early.

## Control Endpoint Rules

The adapter does these things. The core relies on them.

- On every bus reset, the adapter opens endpoint 0 (IN and OUT, 64 bytes) and sets the address to 0. Then it calls `onBusReset`.
- The adapter receives SETUP packets on endpoint 0 itself. The core never arms anything for them. `onSetup` fires for each one.
- A SETUP while a transfer is in flight on endpoint 0 abandons that transfer in both directions, with no completion event. It also clears a halt on endpoint 0. The port does this itself.
- Completions the hardware finished before the SETUP are reported before `onSetup`. No endpoint 0 completion is reported after `onSetup` for an earlier transfer.
- A SETUP discards a pending address.
- SET_ADDRESS: the core calls `SetAddress()` when the request arrives. The port applies the address before it reports the next completion of `0x80`, which is the status stage of that request. A SETUP that comes first discards it.

The stages of a control request, as the core runs them:

| Stage | Transfer |
|-------|----------|
| Data IN | `StartInTransfer(0x80, data, n < wLength)`. Completion of `0x80` starts `StartOutTransfer(0x00, empty)` |
| Data OUT | `StartOutTransfer(0x00, buffer)`. Completion of `0x00` starts `StartInTransfer(0x80, empty, false)` |
| No data | `StartInTransfer(0x80, empty, false)` |

A rejected request is answered with `Stall(0x80)`. That halts both endpoint 0 directions.

## Context Rules

- Either context: `OpenEndpoint()`, `CloseEndpoint()`, `CloseAllEndpoints()`, `StartInTransfer()`, `StartOutTransfer()`, `Stall()`, `ClearStall()`, `IsStalled()`, `SetAddress()`, `RemoteWakeup()`, `GetFrameNumber()`.
- Thread only: `SetParameters()`, `Connect()`, `Disconnect()`.
- The port takes no critical section inside these calls. A synchronous completion of `StartOutTransfer()` therefore runs inside the caller's section, and the core's callback may open a section of its own, which nests.
- A call from the thread can overlap the interrupt that completes the same endpoint. The caller wraps such a call in `System::CriticalSection(true)` and `(false)`. The core does this for `Stop()`.

## Lifecycle

1. Clock and interrupt in the BSP (see the port page).
2. `SetParameters()`: initializes the controller. Returns `notReady` if the USB clock is off.
3. Assign the events.
4. `Connect()`: the host sees the device.
5. Each bus reset opens endpoint 0 again.
6. `Disconnect()` detaches. The core closes the endpoints.

## Ports

| Port | Class | Status |
|------|-------|--------|
| STM32G4, `fsdev` | [USBDeviceAdapterG4](Port/STM32/G4/USBDeviceAdapterG4.md), `AUSB` | In the tree |
| STM32G0 `fsdev` (G0B1, G0C1) | Not yet | Planned |
| STM32F4 OTG (`dwc2`) | Not yet | Planned |
| ESP32-S3 and P4 (`dwc2`) | Not yet | Later |
