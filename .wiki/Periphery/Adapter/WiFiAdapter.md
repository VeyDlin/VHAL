# WiFiAdapter

Abstract adapter for Wi-Fi link control: scanning, joining an access point, running an access point, and link events. It carries no data. TCP, UDP and TLS go through [SocketAdapter](SocketAdapter.md), so the same application code runs over any link.

Header: `#include <Adapter/WiFiAdapter.h>`

## Not a Template

`WiFiAdapter` is a plain class, not a template. A board can hold a built-in radio and an external module at once, and portable code takes a `WiFiAdapter&` without knowing which one it got. An external module driver implements the same interface.

## Enums

| Enum | Values | Description |
|------|--------|-------------|
| `Interface` | `Station`, `AccessPoint` | Interface selector for `GetMacAddress()` and `GetIpInfo()` |
| `Security` | `Open`, `WEP`, `WPA`, `WPA2`, `WPA2WPA3`, `WPA3`, `Enterprise`, `Unknown` | Access point security |
| `PowerSave` | `None`, `Minimum`, `Maximum` | Station power saving |
| `State` | `Stopped`, `Idle`, `Connecting`, `Linked`, `Ready` | Station state, see [State Machine](#state-machine) |
| `Event` | `Connected`, `Disconnected`, `GotIp`, `LostIp`, `AccessPointStarted`, `AccessPointStopped`, `ClientConnected`, `ClientDisconnected` | Link events, see [Callbacks](#callbacks) |
| `DisconnectReason` | `None`, `Requested`, `AuthenticationFailed`, `NotFound`, `ConnectionLost`, `Unknown` | Reason of the last disconnect |

## Parameters

```cpp
struct Parameters {
    PowerSave powerSave = PowerSave::Minimum;
    const char *hostname = nullptr;
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `powerSave` | `Minimum` | Station power saving mode |
| `hostname` | `nullptr` | Station host name. `nullptr` keeps the port default |

## StationConfig

```cpp
struct StationConfig {
    const char *ssid = nullptr;
    const char *password = nullptr;
    const MacAddress *bssid = nullptr;
    Security minSecurity = Security::WPA2;
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `ssid` | `nullptr` | Network name, 1 to 32 bytes |
| `password` | `nullptr` | `nullptr` or `""` joins an open network. Otherwise 8 to 64 bytes |
| `bssid` | `nullptr` | Set to join one specific access point |
| `minSecurity` | `WPA2` | Weakest security accepted when a password is given. Ignored for an open network |

## AccessPointConfig

```cpp
struct AccessPointConfig {
    const char *ssid = nullptr;
    const char *password = nullptr;
    uint8 channel = 1;
    uint8 maxClients = 4;
    bool hidden = false;
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `ssid` | `nullptr` | Network name, 1 to 32 bytes |
| `password` | `nullptr` | `nullptr` or `""` starts an open access point. Otherwise 8 to 63 bytes on the ESP32 port |
| `channel` | `1` | Radio channel. `0` is rejected |
| `maxClients` | `4` | Maximum number of stations. `0` is rejected |
| `hidden` | `false` | Do not broadcast the SSID |

## AccessPointInfo

Filled by `Scan()` for each access point found.

```cpp
struct AccessPointInfo {
    char ssid[maxSsidLength + 1] = {};
    MacAddress bssid;
    int8 rssi = 0;
    uint8 channel = 0;
    Security security = Security::Unknown;
};
```

| Field | Description |
|-------|-------------|
| `ssid` | Network name, null-terminated |
| `bssid` | Access point MAC address |
| `rssi` | Signal strength in dBm |
| `channel` | Primary channel |
| `security` | Access point security |

## IpInfo

```cpp
struct IpInfo {
    Ipv4Address address;
    Ipv4Address mask;
    Ipv4Address gateway;
};
```

| Field | Description |
|-------|-------------|
| `address` | IPv4 address |
| `mask` | Subnet mask |
| `gateway` | Default gateway |

`MacAddress` and `Ipv4Address` are defined in `Adapter/Utilities/NetworkTypes.h`. Both hold no heap memory and can be passed by value. `Ipv4Address` takes its octets in written order: `Ipv4Address(192, 168, 1, 10)`.

## State Machine

| From | To | Trigger |
|------|----|---------|
| `Stopped` | `Idle` | `SetParameters()` (first call, or after `Stop()`) |
| `Idle` | `Connecting` | `ConnectAsync()` |
| `Connecting` | `Linked` | Associated with the access point (`Connected` event) |
| `Linked` | `Ready` | IP address assigned (`GotIp` event) |
| `Ready` | `Linked` | IP address removed (`LostIp` event) |
| `Connecting`, `Linked`, `Ready` | `Idle` | Link lost or join failed (`Disconnected` event) |
| `Connecting`, `Linked`, `Ready` | `Idle` | `Disconnect()`. No event is raised |
| Any | `Stopped` | `Stop()` |

There is no automatic reconnect. After `Disconnected`, the application decides whether to call `ConnectAsync()` again.

## API

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(Parameters val)` | `ResultStatus` | Apply parameters and initialize the port. Turns the radio on when it is `Stopped` |
| `GetParameters()` | `const Parameters&` | Current parameters |
| `SetTimeout(uint32 val)` | `void` | Limit for `Connect()`, in milliseconds (default `15000`) |
| `GetState()` | `State` | Current station state |
| `GetLastDisconnectReason()` | `DisconnectReason` | Reason of the last disconnect. `None` after a new join starts |
| `Connect(const StationConfig& config)` | `ResultStatus` | Join and block until `Ready`, failure or timeout |
| `ConnectAsync(const StationConfig& config)` | `ResultStatus` | Start a join and return. The outcome arrives through `onEvent` and `GetState()` |
| `Disconnect()` | `ResultStatus` | Leave the joined access point |
| `Scan(onResult)` | `Result<uint16>` | Blocking scan. Calls `onResult` once per access point and returns their number |
| `StartAccessPoint(const AccessPointConfig& config)` | `ResultStatus` | Start an access point next to the station |
| `StopAccessPoint()` | `ResultStatus` | Stop the access point. The station stays enabled |
| `Stop()` | `ResultStatus` | Turn the radio off. `SetParameters()` turns it on again |
| `GetRssi()` | `Result<int8>` | Signal strength of the joined access point, in dBm |
| `GetMacAddress(Interface interface = Station)` | `Result<MacAddress>` | MAC address of the interface |
| `GetIpInfo(Interface interface = Station)` | `Result<IpInfo>` | IP configuration of the interface |
| `SetPowerSave(PowerSave val)` | `ResultStatus` | Change the power saving mode |
| `SetTxPower(int8 dBm)` | `ResultStatus` | Set the maximum transmit power, see [Value Ranges](#value-ranges) |
| `Process()` | `void` | Poll a radio that has no task of its own. Empty on ports with their own task, safe to call always |

`Connect()` polls the state and calls `Process()` while it waits, so a port without its own task is driven by it.

## Callbacks

| Callback | Type | Description |
|----------|------|-------------|
| `onEvent` | `std::function<void(Event event)>` | Link event |

Context rules for `onEvent`:

- It runs in the context of the radio stack: its own task or `Process()`, not in the caller's context.
- Do not block in it.
- Do not call `Connect()` or `Scan()` from it. Signal a thread instead.

| Event | Raised when |
|-------|-------------|
| `Connected` | Associated with the access point. State becomes `Linked` |
| `Disconnected` | The link was lost or the join failed. State becomes `Idle`. Read `GetLastDisconnectReason()` for the cause |

The events are raised by the ESP32 port. See [ESP32 Port](#esp32-port) for the details.
| `GotIp` | An IP address was assigned |
| `LostIp` | The IP address was removed |
| `AccessPointStarted` | The access point is running |
| `AccessPointStopped` | The access point stopped |
| `ClientConnected` | A station joined the access point |
| `ClientDisconnected` | A station left the access point |

## ESP32 Port

`AWIFI` is `WiFiAdapterESP` (`Periphery/Adapter/Port/ESP32/Common/WiFiAdapterESP.h`), built on the ESP-IDF `esp_wifi` driver. The chip has one radio, so the application holds one instance. Enabled by `#define VHAL_ESP32_WIFI`.

Header: `#include <Adapter/Port/ESP32/Common/WiFiAdapterESP.h>`

### Constructor

```cpp
WiFiAdapterESP();
```

The copy constructor and copy assignment are deleted.

### SetParameters

`SetParameters()` runs the port initialization:

1. `beforePeripheryInit` runs, if set. A non-`ok` result is returned.
2. The driver is initialized once: NVS is initialized, the network interface and default event loop are created, the Wi-Fi driver is initialized with RAM storage, and the Wi-Fi and IP event handlers are registered. Later calls skip this step.
3. `parameters.hostname` is applied to the station interface, if set.
4. If the state is `Stopped`, the station mode is set, the radio starts, and the state becomes `Idle`.
5. `parameters.powerSave` is applied.
6. `afterPeripheryInit` runs, if set.

On a running adapter `SetParameters()` does not restart the radio. It applies the host name and the power saving mode only.

### ESP-IDF Components

Add these to `REQUIRES` in the component `CMakeLists.txt`:

```cmake
REQUIRES esp_wifi esp_netif esp_event nvs_flash
```

### Behaviour

- The station interface stays enabled, so `Scan()` works while an access point runs.
- Credentials are kept in RAM only. The driver's NVS storage is off (`nvs_enable = 0`, RAM storage). NVS is initialized only to speed up radio calibration. It is never erased. Saving credentials is the application's job.
- Events are raised from the ESP-IDF default event loop task.
- `Process()` does nothing: the driver has its own task.
- `Disconnect()` settles the state and the reason itself. The driver raises no `Disconnected` event for it.
- A late disconnect with reason `ASSOC_LEAVE` during `Connecting` (the previous link being dropped by `ConnectAsync()`) raises no event and does not fail the new attempt.
- There is no automatic reconnect.

### Disconnect Reasons

| Driver reason | `DisconnectReason` |
|---------------|--------------------|
| `AUTH_EXPIRE`, `AUTH_FAIL`, `4WAY_HANDSHAKE_TIMEOUT`, `HANDSHAKE_TIMEOUT` | `AuthenticationFailed` |
| `NO_AP_FOUND` | `NotFound` |
| `BEACON_TIMEOUT` | `ConnectionLost` |
| `ASSOC_LEAVE` | `Requested` |
| Any other | `Unknown` |

`Connect()` returns these statuses when the join ends in `Idle`:

| `DisconnectReason` | `ResultStatus` |
|--------------------|----------------|
| `AuthenticationFailed` | `authenticationFailed` |
| `NotFound` | `notFound` |
| `ConnectionLost` | `signalLost` |
| Any other | `noConnection` |

`Connect()` returns `timeout` when `SetTimeout()` expires. It calls `Disconnect()` first.

### Return Values

| Method | Status | When |
|--------|--------|------|
| `ConnectAsync()` | `noInit` | State is `Stopped` |
| | `invalidParameter` | SSID not 1 to 32 bytes, password not empty and not 8 to 64 bytes, or the driver rejects the configuration |
| | `error` | The driver failed to start the join. State returns to `Idle` |
| `Scan()` | `noInit` | State is `Stopped` |
| | `busy` | State is `Connecting` |
| | `error` | The driver scan failed |
| `StartAccessPoint()` | `noInit` | State is `Stopped` |
| | `invalidParameter` | SSID, password (empty or 8 to 63 bytes), `channel` `0` or `maxClients` `0`, or the driver rejects the configuration |
| | `error` | Changing the Wi-Fi mode failed |
| `StopAccessPoint()` | `ok` | No access point is running |
| | `noInit` | State is `Stopped` |
| `Stop()` | `ok` | State is already `Stopped` |
| `Disconnect()` | `noInit` | State is `Stopped` |
| `GetRssi()` | `noConnection` | State is not `Linked` or `Ready`, or no access point information |
| `GetMacAddress()` | `noInit` | Driver not initialized, before the first `SetParameters()` |
| `GetIpInfo()` | `noInit` | Network interface not created |
| `SetPowerSave()` | `noInit` | Driver not initialized |
| `SetTxPower()` | `noInit` | State is `Stopped` |
| | `outOfRange` | Outside 2 to 20 dBm |
| `SetParameters()` | `invalidParameter` | The host name could not be set |
| | `outOfMemory` | The network interfaces could not be created |

### Value Ranges

| Value | Range | Checked by |
|-------|-------|------------|
| SSID (station and access point) | 1 to 32 bytes | `invalidParameter` |
| Station password | empty, or 8 to 64 bytes | `invalidParameter` |
| Access point password | empty, or 8 to 63 bytes | `invalidParameter` |
| Access point `channel` | not `0` | `invalidParameter` |
| Access point `maxClients` | not `0` | `invalidParameter` |
| `SetTxPower()` | 2 to 20 dBm | `outOfRange` |

`SetTxPower()` rounds down to a level the radio supports. The driver counts in units of 0.25 dBm. The call is valid only while the radio is started.

## BSP Configuration Example

```cpp
// BSP.h
class BSP {
public:
    static AWIFI wifi;
};

// BSP.cpp
AWIFI BSP::wifi;
```

## Usage Example

Join a network from a `ThreadStatic` task, then scan. `Connect()` blocks, so it must not run in the event callback.

```cpp
// WifiTask.h
#pragma once
#include <Application.h>


class WifiTask : public ThreadStatic<4096> {
public:
    void Execute() override {
        BSP::wifi.SetParameters({
            .powerSave = AWIFI::PowerSave::Minimum,
            .hostname = "sensor-1"
        });

        AWIFI::StationConfig config = {
            .ssid = "HomeNet",
            .password = "correct-horse-battery"
        };

        if (BSP::wifi.Connect(config) != ResultStatus::ok) {
            // Check BSP::wifi.GetLastDisconnectReason() and retry later
            return;
        }

        Result<uint16> count = BSP::wifi.Scan([](const AWIFI::AccessPointInfo& info) {
            // info.ssid, info.rssi (dBm), info.channel, info.security
        });

        if (count.IsErr()) {
            return;
        }
    }
};
```

The task is created in the application's `Init()` with `RTOS::CreateThread(wifiTask, ThreadPriority::normal, "wifi");`, as in the ESP32 demo. `Scan()` returns `busy` while the station is `Connecting`.
