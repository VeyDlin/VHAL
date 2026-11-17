# BLEAdapter

Abstract adapter for Bluetooth Low Energy. It covers the peripheral role (advertising and a GATT server) and the observer role (scanning). It does not cover the central role: the adapter does not connect to other devices, and there is no GATT client in this version.

Header: `#include <Adapter/BLEAdapter.h>`

## Not a Template

`BLEAdapter` is a plain class, not a template. A chip's built-in radio and an external BLE driver both implement it, and portable code takes a `BLEAdapter&` without knowing which one it got.

All BLE types are nested in the class (`BLEAdapter::Uuid`, `BLEAdapter::Service`, `BLEAdapter::Characteristic`, and so on). A driver's types cannot clash with those of another driver, and each type is reached through the adapter it belongs to.

## GATT Table Lifecycle

The GATT table is declared before the radio starts and is fixed after that:

1. Create the `Service` and `Characteristic` objects. They must outlive the radio.
2. Register each service with `AddService()`, and add its characteristics with `Service::AddCharacteristic()`.
3. Call `SetParameters()`. It builds the table and starts the radio.
4. Once the radio is started, the table is fixed. `AddService()` returns `busy`.

Characteristic values can still change after the start, with `Set()` or `SetValue()`. `Notify()` sends the current value to the subscribed peers.

On the ESP32 port, `Stop()` is not supported, so the table cannot be changed for the rest of the run. See [ESP32 Port](#esp32-port).

## Enums

| Enum | Values | Description |
|------|--------|-------------|
| `AddressType` | `Public`, `Random` | Kind of a device address |
| `Security` | `Open`, `Encrypted`, `Authenticated` | Access requirement of a characteristic, see [Security](#security) |
| `Event` | `Connected`, `Disconnected`, `AdvertisingStopped`, `ScanStopped`, `MtuChanged`, `SecurityChanged` | Link and radio events, see [Events](#events) |
| `IoCapability` | `None`, `DisplayOnly`, `KeyboardOnly`, `DisplayYesNo`, `KeyboardDisplay` | Input and output capability used for pairing, see [Parameters](#parameters) |

## Types

### Uuid

```cpp
struct Uuid {
    static constexpr uint8 maxSize = 16;
    uint8 bytes[maxSize];       // Most significant byte first
    uint8 size;                 // 2 or 16. 0 is an empty UUID
};
```

| Member | Description |
|--------|-------------|
| `Uuid()` | Empty UUID, `size` `0` |
| `Uuid(uint16 shortUuid)` | 16-bit UUID, `size` `2`. `bytes[0]` holds the high byte |
| `static FromString(const char *text)` | `constexpr`. Takes 32 hex digits. Dashes are ignored anywhere, and letter case does not matter. Any other input, including `nullptr` or a wrong digit count, gives an empty UUID. A valid string gives `size` `16` |
| `IsValid()` | `true` when `size` is `2` or `16` |
| `operator==`, `operator!=` | Compare `size` and the bytes |

Byte order: `bytes` holds the UUID in the order it is written. `Uuid::FromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")` gives `bytes[0]` = `0x6E`. The ESP32 port converts to the byte order NimBLE uses, so the application always works in the written order.

### Address

```cpp
struct Address {
    MacAddress address;
    AddressType type = AddressType::Public;
};
```

| Field | Description |
|-------|-------------|
| `address` | Device address. `address.bytes[0]` is the first octet as printed. `MacAddress` is defined in `Adapter/Utilities/NetworkTypes.h` |
| `type` | `Public` or `Random` |

### Properties

```cpp
struct Properties {
    bool read = false;
    bool write = false;
    bool writeWithoutResponse = false;
    bool notify = false;
    bool indicate = false;
};
```

| Field | Description |
|-------|-------------|
| `read` | Peers can read the value |
| `write` | Peers can write the value, with a response |
| `writeWithoutResponse` | Peers can write the value, without a response |
| `notify` | The value can be sent as a notification |
| `indicate` | The value can be sent as an indication |

All fields default to `false`.

### Security

Access requirement of a characteristic. `readSecurity` applies when `properties.read` is set. `writeSecurity` applies when `properties.write` or `properties.writeWithoutResponse` is set.

| Value | Requirement |
|-------|-------------|
| `Open` | None. The default |
| `Encrypted` | A bonded or paired link |
| `Authenticated` | A bonded or paired link, and the pairing was protected against a man in the middle |

### Characteristic

```cpp
class Characteristic {
public:
    Uuid uuid;
    Properties properties;
    Security readSecurity = Security::Open;
    Security writeSecurity = Security::Open;
    std::function<void(ConnectionHandle connection)> onRead;
    std::function<void(ConnectionHandle connection)> onWrite;
    std::function<void(ConnectionHandle connection, bool notify, bool indicate)> onSubscribe;

    Characteristic(Uuid uuid, Properties properties, uint8 *buffer, uint16 capacity);
};
```

The value is stored in the caller's buffer. The buffer must outlive the characteristic.

| Field | Description |
|-------|-------------|
| `uuid`, `properties` | Set by the constructor |
| `readSecurity`, `writeSecurity` | Access requirement, see [Security](#security) |
| `onRead` | Runs before the value is sent to the peer. It can call `Set()` to refresh the value |
| `onWrite` | Runs after a remote write is stored |
| `onSubscribe` | Runs when a peer changes its subscription. `notify` and `indicate` are the flags the peer now has set |

| Method | Return | Description |
|--------|--------|-------------|
| `SetValue(const uint8 *data, uint16 size)` | `ResultStatus` | Store `size` bytes. `outOfRange` when `size` is above the capacity. `invalidParameter` when `data` is `nullptr` and `size` is above `0`. A `size` of `0` empties the value |
| `Set<T>(const T& value)` | `ResultStatus` | Store the bytes of a trivially copyable object. Same statuses as `SetValue()` |
| `GetValue()` | `const uint8 *` | The buffer. Only the first `GetLength()` bytes are valid |
| `GetLength()` | `uint16` | Length of the stored value |
| `GetCapacity()` | `uint16` | Size of the buffer |
| `Get<T>()` | `Result<T>` | The stored value as `T`. `invalidData` when the stored length is not `sizeof(T)` |

The port also uses `GetStorage()`, `SetLength()`, `valueHandle` and `next`. Applications do not call them.

### CharacteristicStatic

```cpp
template<uint16 Capacity>
class CharacteristicStatic : public Characteristic {
public:
    CharacteristicStatic(Uuid uuid, Properties properties);
};
```

Same as `Characteristic`, but the buffer of `Capacity` bytes is part of the object. The object must still outlive the radio, so keep it static or in a long-lived object.

```cpp
static BLEAdapter::CharacteristicStatic<4> level(BLEAdapter::Uuid(0x2A6F), { .read = true, .notify = true });
```

### Service

```cpp
class Service {
public:
    Uuid uuid;
    explicit Service(Uuid uuid);
    void AddCharacteristic(Characteristic& characteristic);
};
```

| Member | Description |
|--------|-------------|
| `uuid` | Service UUID |
| `AddCharacteristic()` | Appends the characteristic to the service. A characteristic belongs to one service. Adding one that is already in the list does nothing. No status is returned |

The `characteristics` and `next` members are used by the port.

### Parameters

```cpp
struct Parameters {
    const char *deviceName = "VHAL";
    uint16 appearance = 0;
    bool bonding = false;
    bool secureConnections = true;
    bool mitm = false;
    IoCapability ioCapability = IoCapability::None;
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `deviceName` | `"VHAL"` | GAP device name. `nullptr` sets no name and advertises none |
| `appearance` | `0` | GAP appearance code |
| `bonding` | `false` | Keep the keys of a paired peer, so it can reconnect with encryption |
| `secureConnections` | `true` | Use LE Secure Connections for pairing |
| `mitm` | `false` | Request protection against a man in the middle |
| `ioCapability` | `None` | Input and output capability of this device. Only the passkey display action is handled, see [Callbacks](#callbacks) |

### AdvertisingConfig

```cpp
struct AdvertisingConfig {
    bool connectable = true;
    uint16 intervalMinMs = 100;
    uint16 intervalMaxMs = 150;
    uint32 durationMs = 0;
    bool includeName = true;
    const Uuid *serviceUuids = nullptr;
    uint8 serviceUuidCount = 0;
    const uint8 *manufacturerData = nullptr;
    uint8 manufacturerDataSize = 0;
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `connectable` | `true` | `false` advertises without accepting connections |
| `intervalMinMs` | `100` | Shortest advertising interval, in milliseconds. The ESP32 range is in [ESP32 Port](#limits) |
| `intervalMaxMs` | `150` | Longest advertising interval, in milliseconds |
| `durationMs` | `0` | Advertising time, in milliseconds. `0` advertises until stopped. A value above the NimBLE limit `BLE_HS_FOREVER` also runs without a limit |
| `includeName` | `true` | Advertise the device name. It goes into the scan response when it does not fit the advertisement |
| `serviceUuids` | `nullptr` | Service UUIDs to advertise. Only needed during the call |
| `serviceUuidCount` | `0` | Number of entries in `serviceUuids`. Limit: `maxAdvertisedUuids` |
| `manufacturerData` | `nullptr` | Manufacturer-specific data. Only needed during the call |
| `manufacturerDataSize` | `0` | Length of `manufacturerData` |

### ScanConfig

```cpp
struct ScanConfig {
    bool active = false;
    uint16 intervalMs = 100;
    uint16 windowMs = 50;
    uint32 durationMs = 0;
    bool filterDuplicates = true;
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `active` | `false` | `true` for an active scan, `false` for a passive scan |
| `intervalMs` | `100` | Time between the starts of two scan windows, in milliseconds |
| `windowMs` | `50` | Length of one scan window, in milliseconds. Must not be above `intervalMs` |
| `durationMs` | `0` | Scan time, in milliseconds. `0` scans until stopped. As for advertising, a value above `BLE_HS_FOREVER` runs without a limit |
| `filterDuplicates` | `true` | Report each device once, not once per advertisement |

### ScanResult

```cpp
struct ScanResult {
    Address address;
    int8 rssi = 0;
    const uint8 *data = nullptr;
    uint8 dataSize = 0;
    bool connectable = false;
    bool scanResponse = false;
};
```

| Field | Description |
|-------|-------------|
| `address` | Address of the advertising device |
| `rssi` | Signal strength in dBm |
| `data` | Advertising data. Valid only during the callback. Copy it to keep it |
| `dataSize` | Length of `data` |
| `connectable` | `true` for connectable advertising |
| `scanResponse` | `true` when the report is a scan response |

### FindAdvertisingField

```cpp
static bool FindAdvertisingField(const uint8 *data, uint8 dataSize, uint8 type, const uint8 *&field, uint8 &fieldSize);
```

Walks the length, type and value structures of advertising data and returns the first field of `type`. `field` points into `data`, and `fieldSize` is the length of the value without the type byte. It returns `false` when the field is not there, at the zero-length padding, or when a structure runs past `dataSize`. The function is portable, so it works on every port.

| Constant | Value | Field |
|----------|-------|-------|
| `fieldShortName` | `0x08` | Shortened local name |
| `fieldCompleteName` | `0x09` | Complete local name |
| `fieldManufacturerData` | `0xFF` | Manufacturer-specific data |

## Events

`onEvent` receives an `Event` and a connection handle. `invalidConnection` (`0xFFFF`) is passed when no connection is involved.

| Event | Raised when (ESP32 port) |
|-------|--------------------------|
| `Connected` | A connection was established |
| `Disconnected` | A connection ended |
| `AdvertisingStopped` | NimBLE reports that advertising ended, or a connection attempt failed. Connection is `invalidConnection` |
| `ScanStopped` | The scan completed. Connection is `invalidConnection` |
| `MtuChanged` | The ATT MTU of a connection changed |
| `SecurityChanged` | The encryption state of a connection changed |

The adapter does not restart advertising after a disconnect. The application calls `StartAdvertising()` again when it wants the device to be visible.

## API

| Method | Return | Description | Statuses |
|--------|--------|-------------|----------|
| `AddService(Service& service)` | `ResultStatus` | Append the service to the GATT table. Adding one already registered does nothing | `ok`, `busy` after start |
| `SetParameters(Parameters val)` | `ResultStatus` | Store the parameters, build the GATT table and start the radio | See [Return Values](#return-values) |
| `GetParameters()` | `const Parameters&` | Current parameters | None |
| `IsStarted()` | `bool` | `true` after the radio has started | None |
| `Stop()` | `ResultStatus` | Turn the radio off | `notSupported` on ESP32 |
| `StartAdvertising(const AdvertisingConfig& config)` | `ResultStatus` | Start advertising | `noInit`, `invalidParameter`, `outOfRange`, `busy`, `error`, `outOfMemory` |
| `StopAdvertising()` | `ResultStatus` | Stop advertising. `ok` when not advertising | `noInit`, `error`, and the NimBLE mapping |
| `Notify(Characteristic& characteristic)` | `ResultStatus` | Send the current value to every subscribed peer, as a notification or an indication according to the peer's subscription | `ok`, `noInit`, `notExist` |
| `Disconnect(ConnectionHandle connection)` | `ResultStatus` | Terminate a connection | `noInit`, `noConnection`, `invalidParameter`, `error` |
| `GetRssi(ConnectionHandle connection)` | `Result<int8>` | Signal strength of a connection, in dBm | `noInit`, `noConnection`, `invalidParameter`, `error` |
| `GetMtu(ConnectionHandle connection)` | `Result<uint16>` | ATT MTU of a connection | `noInit`, `noConnection` |
| `GetAddress()` | `Result<Address>` | The device's own identity address | `noInit`, `error` |
| `GetPeerAddress(ConnectionHandle connection)` | `Result<Address>` | The identity address of the peer of a connection | `noInit`, `error`, and the NimBLE mapping |
| `StartScan(const ScanConfig& config, onResult)` | `ResultStatus` | Start a scan. `onResult` runs for every advertising report until the scan stops | `noInit`, `invalidParameter`, `busy`, `error` |
| `StopScan()` | `ResultStatus` | Stop the scan. `ok` when no scan is running | `noInit`, `error`, and the NimBLE mapping |
| `SetTxPower(int8 dBm)` | `ResultStatus` | Set the default transmit power, see [Value Ranges](#value-ranges) | `noInit`, `outOfRange`, `error` |
| `Process()` | `void` | Poll a radio that has no task of its own. Empty on ESP32 | None |

`Process()` is always safe to call. The ESP32 port runs NimBLE on its own task, so `Process()` does nothing there.

## Callbacks

| Callback | Type | Description |
|----------|------|-------------|
| `onEvent` | `std::function<void(Event event, ConnectionHandle connection)>` | Event, see [Events](#events) |
| `onPasskeyDisplay` | `std::function<void(ConnectionHandle connection, uint32 passkey)>` | A passkey the device must show to the peer during pairing |
| `Characteristic::onRead`, `onWrite`, `onSubscribe` | See [Characteristic](#characteristic) | Access and subscription events of one characteristic |
| `StartScan()` `onResult` | `std::function<void(const ScanResult& result)>` | One advertising report |

Context rules: the callbacks run in the context of the BLE host task, not the caller's. Do not block in them. The value of a characteristic is not locked, so the application must not call `SetValue()` or `Set()` while a remote write to the same characteristic can be in progress, unless it synchronises the two itself.

## ESP32 Port

`ABLE` is `BLEAdapterESP` (`Periphery/Adapter/Port/ESP32/Common/BLEAdapterESP.h`), built on the NimBLE host over the ESP32 Bluetooth controller. The radio is one, so the application holds one instance. Enabled by `#define VHAL_ESP32_BLE`.

Header: `#include <Adapter/Port/ESP32/Common/BLEAdapterESP.h>`

The port is checked by compile only, with the call list in the harness check `CheckBle.cpp`. The behaviour on this page comes from the source. It has not been run on hardware.

### Constructor

```cpp
BLEAdapterESP();
```

The copy constructor and copy assignment are deleted. The destructor clears the instance that the NimBLE callbacks use.

### SetParameters

`SetParameters()` stores the parameters, then runs the port initialization:

1. `beforePeripheryInit` runs, if set. A non-`ok` result is returned and nothing else is done.
2. If the radio is already started, `busy` is returned.
3. The GATT table is built from the registered services. This only touches memory, so a table that is too large is refused before the radio is initialized.
4. `nvs_flash_init()` and `nimble_port_init()` run. The NVS result is not checked.
5. The host is configured: reset and sync callbacks, IO capability, bonding, MITM protection and Secure Connections. With `bonding`, the encryption keys are distributed in both directions.
6. The GATT services are registered. The device name is set, unless it is `nullptr`, and then the appearance is set.
7. The NimBLE host task starts. The call waits up to 5000 ms for the host to sync with the controller, and returns `timeout` if it does not.
8. The own address type is chosen with `ble_hs_id_infer_auto()`. Advertising, scanning and `GetAddress()` use it.
9. The radio is marked as started, and `afterPeripheryInit` runs, if set.

### ESP-IDF Components

Add these to `REQUIRES` in the component `CMakeLists.txt`:

```cmake
REQUIRES bt nvs_flash
```

`bt` provides the NimBLE host and the controller API (`esp_bt.h`). `nvs_flash` is needed because the port calls `nvs_flash_init()`.

### sdkconfig Options

| Option | Value | Checked by |
|--------|-------|------------|
| `CONFIG_BT_ENABLED` | `y` | Named in the `#error` message. Not tested by a condition |
| `CONFIG_BT_NIMBLE_ENABLED` | `y` | `#error` in `Platform.h` when it is off |
| `CONFIG_BT_NIMBLE_GAP_SERVICE` | `y` | `#error` in `Platform.h` when it is off |

The compile-only check's `sdkconfig.defaults` also sets the controller to BLE only (`CONFIG_BTDM_CTRL_MODE_BLE_ONLY=y`, `CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=n`, `CONFIG_BTDM_CTRL_MODE_BTDM=n`), and turns Bluedroid off (`CONFIG_BT_BLUEDROID_ENABLED=n`). Its comments say that the NimBLE host and Wi-Fi do not fit a 1.5 MB app partition, so it uses 8 MB flash and size optimisation. They also say that Wi-Fi and BLE in one image overflow IRAM by about 18 KB.

### Limits

| Limit | Value | Enforced by |
|-------|-------|-------------|
| `maxServices` | `8` | `SetParameters()`, `outOfRange` |
| `maxCharacteristics` | `32`, all services together | `SetParameters()`, `outOfRange` |
| `maxAdvertisedUuids` | `8` | `StartAdvertising()`, `outOfRange` |
| Advertising interval | `minIntervalMs` `20` to `maxIntervalMs` `10240`, and `intervalMinMs` not above `intervalMaxMs` | `StartAdvertising()`, `invalidParameter` |
| Scan interval | Not above `10240` ms | `StartScan()`, `invalidParameter` |
| Scan window | Not `0`, and not above the interval | `StartScan()`, `invalidParameter` |
| Device name in advertising | Not above 255 bytes | `StartAdvertising()`, `invalidParameter` |
| Host sync | 5000 ms | `SetParameters()`, `timeout` |

The static limits are `BLEAdapterESP::maxServices`, `maxCharacteristics`, `maxAdvertisedUuids`, `minIntervalMs` and `maxIntervalMs`.

### Value Ranges

| Value | Range | Checked by |
|-------|-------|------------|
| `SetTxPower()` | Not below `-12` dBm | `outOfRange` |

The controller takes eight levels: `-12`, `-9`, `-6`, `-3`, `0`, `3`, `6` and `9` dBm. `SetTxPower()` selects the highest level that is not above the request. A request above `+9` dBm selects `+9` dBm, and the power is never raised above the request. The level applies to the default power type.

### Status Mapping

Some calls return the NimBLE result through this mapping:

| NimBLE error | `ResultStatus` |
|--------------|----------------|
| None | `ok` |
| `BLE_HS_EALREADY`, `BLE_HS_EBUSY` | `busy` |
| `BLE_HS_ENOTCONN` | `noConnection` |
| `BLE_HS_EINVAL`, `BLE_HS_EMSGSIZE` | `invalidParameter` |
| `BLE_HS_ENOMEM` | `outOfMemory` |
| Any other | `error` |

### Return Values

| Method | Status | When |
|--------|--------|------|
| `SetParameters()` | Hook status | `beforePeripheryInit` returns a non-`ok` status |
| | `busy` | The radio is already started |
| | `outOfRange` | More than 8 services, or more than 32 characteristics |
| | `invalidParameter` | A UUID is neither 2 nor 16 bytes, or NimBLE rejects a call through the mapping |
| | `timeout` | The host did not sync within 5000 ms |
| | `error` | `nimble_port_init()` failed, or NimBLE returned an unmapped error |
| `AddService()` | `busy` | The radio is started. UUIDs are not checked here, but in `SetParameters()` |
| `StartAdvertising()` | `noInit` | The radio is not started |
| | `invalidParameter` | Interval outside 20 to 10240 ms, or minimum above maximum. A UUID is neither 2 nor 16 bytes. `serviceUuids` is `nullptr` with a count above `0`. `manufacturerData` is `nullptr` with a size above `0`. The device name is longer than 255 bytes and is advertised |
| | `outOfRange` | `serviceUuidCount` is above 8 |
| | `busy` | NimBLE returns `EALREADY` or `EBUSY` |
| `StopAdvertising()` | `noInit` | The radio is not started |
| | `ok` | Not advertising |
| `Notify()` | `noInit` | The radio is not started |
| | `notExist` | The characteristic has no value handle yet, because it is not in the table |
| `Disconnect()` | `noInit` | The radio is not started |
| `GetMtu()` | `noInit` | The radio is not started |
| | `noConnection` | No MTU for the connection handle |
| `StartScan()` | `noInit` | The radio is not started |
| | `invalidParameter` | `onResult` is `nullptr`, interval or window is `0`, window is above interval, or interval is above 10240 ms |
| | `busy` | A scan is already running |
| `StopScan()` | `noInit` | The radio is not started |
| | `ok` | No scan is running |
| `SetTxPower()` | `noInit` | The radio is not started |
| | `outOfRange` | Below `-12` dBm |
| | `error` | `esp_ble_tx_power_set()` failed |
| `Stop()` | `notSupported` | Always |
| `Process()` | None | Does nothing |

### Behaviour

- `Stop()` is not supported. The host is not restarted, because that would also mean rebuilding the GATT table. The restart path has not been verified on this port.
- After a failed start, the radio is left initialized and is not retried by the adapter.
- The NimBLE tables point at the application's `Service` and `Characteristic` objects. They are built once, before the host starts, so those objects must outlive the radio.
- All callbacks run in the NimBLE host task. `onEvent`, `onPasskeyDisplay`, the characteristic callbacks and the scan callback are called from there.
- `SetParameters()` on a started radio returns `busy` and changes nothing: `GetParameters()` keeps reporting the parameters the radio uses, and `beforePeripheryInit` does not run.
- A device name that does not fit the advertisement is sent in the scan response. The advertisement carries the flags for general discovery, with BR/EDR not supported.
- A write longer than the characteristic's capacity is refused with an ATT error. The stored value is not changed. A shorter or equal write is stored straight into the buffer.
- `Notify()` does not check the `notify` and `indicate` flags. The stack decides what is sent to each peer.
- `Disconnect()` terminates the connection with the reason "remote user terminated connection".
- `GetPeerAddress()` returns the peer's identity address, not its over-the-air address.
- Passkey: only the display action is handled. The port generates a random number below 1000000, injects it into the pairing, and reports it through `onPasskeyDisplay`. Numeric comparison, keyboard entry and out-of-band data are not handled, so a pairing that needs them waits until the stack times out.
- A peer that pairs again replaces its old bond. This is the NimBLE example policy. It gives up the protection of the old bond, so that a peer which lost its keys can reconnect.
- Only one scan runs at a time. `StartScan()` returns `busy` while one runs. The scan callback is cleared when the scan completes.
- NVS is initialized to speed up radio calibration at boot. It is never erased. Saving the bonds is the application's job.

### Limitations

- No central role: the adapter does not connect to other devices.
- No GATT client.
- `Stop()` is not supported, so the GATT table cannot be changed after the first start.
- Only the passkey display pairing action is handled.
- The adapter does not restart advertising after a disconnect.
- Only one scan at a time.
- The limits in [Limits](#limits) are fixed.

## BSP Configuration Example

```cpp
// BSP.h
class BSP {
public:
    static ABLE ble;
};

// BSP.cpp
ABLE BSP::ble;
```

## Usage Example

### Peripheral

A peripheral with one service. The service has a readable and notifying characteristic, and a writable one. The task starts the radio, advertises, and calls `Notify()` once a second. The objects are `static` inside `Execute()`, so they outlive the radio.

```cpp
// PeripheralTask.h
#pragma once
#include <Application.h>


class PeripheralTask : public ThreadStatic<4096> {
public:
    void Execute() override {
        static BLEAdapter::Uuid serviceUuid = BLEAdapter::Uuid::FromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");

        // Readable and notifying: the application sets the value, peers subscribe to it.
        static BLEAdapter::CharacteristicStatic<1> level(BLEAdapter::Uuid(0x2A6F), { .read = true, .notify = true });

        // Writable: a peer writes into it. Read it with command.Get<uint8>().
        static BLEAdapter::CharacteristicStatic<1> command(BLEAdapter::Uuid(0x2A70), { .write = true });

        static BLEAdapter::Service service(serviceUuid);
        service.AddCharacteristic(level);
        service.AddCharacteristic(command);

        if (BSP::ble.AddService(service) != ResultStatus::ok) {
            return;
        }

        if (BSP::ble.SetParameters({ .deviceName = "VHAL-Sensor", .appearance = 0x0340 }) != ResultStatus::ok) {
            return;
        }

        BLEAdapter::AdvertisingConfig advertising = {
            .connectable = true,
            .serviceUuids = &serviceUuid,
            .serviceUuidCount = 1,
        };

        if (BSP::ble.StartAdvertising(advertising) != ResultStatus::ok) {
            return;
        }

        uint8 counter = 0;
        while (true) {
            level.Set<uint8>(counter);
            BSP::ble.Notify(level);
            counter++;
            System::DelayMs(1000);
        }
    }
};
```

Create the task in the application's `Init()`, with the task object as a global, as in the Wi-Fi example: `RTOS::CreateThread(peripheralTask, ThreadPriority::normal, "ble");`. `SetParameters()` can block for up to 5 seconds while the host syncs, so it is called from the task, not from `Init()`.

### Observer

Scan for 5 seconds and print the name of each device that has one. This example uses the radio started by `PeripheralTask`, or starts it if it is not running yet.

```cpp
// ScanTask.h
#pragma once
#include <Application.h>
#include <cstdio>


class ScanTask : public ThreadStatic<4096> {
public:
    void Execute() override {
        if (!BSP::ble.IsStarted() && BSP::ble.SetParameters({}) != ResultStatus::ok) {
            return;
        }

        BLEAdapter::ScanConfig scan = { .active = true };

        ResultStatus status = BSP::ble.StartScan(scan, [](const BLEAdapter::ScanResult& result) {
            // Runs in the BLE host task: keep it short.
            const uint8 *name = nullptr;
            uint8 nameSize = 0;

            if (!BLEAdapter::FindAdvertisingField(result.data, result.dataSize, BLEAdapter::fieldCompleteName, name, nameSize) &&
                !BLEAdapter::FindAdvertisingField(result.data, result.dataSize, BLEAdapter::fieldShortName, name, nameSize)) {
                return;
            }

            printf("%.*s (%d dBm)\n", nameSize, name, result.rssi);
        });

        if (status != ResultStatus::ok) {
            return;
        }

        System::DelayMs(5000);
        BSP::ble.StopScan();
    }
};
```
