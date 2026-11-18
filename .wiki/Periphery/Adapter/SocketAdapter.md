# SocketAdapter

Abstract adapter for one client connection: TCP, UDP or TLS over TCP. The adapter carries the data and does not know which link is underneath. The same code that uses a `SocketAdapter&` runs over Wi-Fi, Ethernet or a modem, as long as the link is up.

The link and the data are separate. [WiFiAdapter](WiFiAdapter.md) controls the link: it scans, joins and runs an access point. `SocketAdapter` only opens, reads and writes one connection. The link must be up before `Open()` is called. The adapter does not bring it up.

One adapter holds one connection. Call `Open()` again to connect to another peer.

Header: `#include <Adapter/SocketAdapter.h>`

The class is not a template, for the same reason as `WiFiAdapter`: an external module with its own TCP/IP stack implements the same interface.

## Enums

| Enum | Values | Description |
|------|--------|-------------|
| `Protocol` | `Tcp`, `Udp`, `Tls` | Transport. `Tls` is TLS over TCP |

## Parameters

```cpp
struct Parameters {
    Protocol protocol = Protocol::Tcp;
    const char *caCertificate = nullptr;
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `protocol` | `Tcp` | Transport used by `Open()` |
| `caCertificate` | `nullptr` | `Tls` only. PEM text of the CA certificate that signs the server. It must stay valid until `Open()` returns. `nullptr` verifies the server against the certificate set built into the port |

## API

| Method | Return | Description | Errors |
|--------|--------|-------------|--------|
| `SetParameters(Parameters val)` | `ResultStatus` | Apply protocol and CA certificate. Closes the current connection | `ok`, or the status of the `beforePeripheryInit` hook on the ESP32 port |
| `GetParameters()` | `const Parameters&` | Current parameters | None |
| `SetTimeout(uint32 val)` | `void` | Limit for `Open()`, one `Read()` and one `Write()`, in milliseconds (default `10000`). Takes effect on the next `Open()` | None |
| `Open(const char *host, uint16 port)` | `ResultStatus` | Connect to `host` and `port`. `host` is a name or a dotted IPv4 address. For `Udp`, it fixes the peer for `Write()` and `Read()`. Closes any open connection first | `noInit`, `invalidParameter`, `notFound`, `noConnection`, `timeout`, `notSupported`, `outOfMemory` |
| `Close()` | `ResultStatus` | Close the connection. Safe to call when nothing is open | `ok` |
| `IsOpen()` | `bool` | `true` while a connection exists | None |
| `Resolve(const char *host)` | `Result<Ipv4Address>` | Look up the IPv4 address of `host` | `noInit`, `invalidParameter`, `notFound` |
| `WriteByteArray(const uint8 *buffer, uint32 size)` | `ResultStatus` | Send the whole buffer or return an error | `noInit`, `timeout`, `writeError` |
| `ReadByteArray(uint8 *buffer, uint32 size)` | `Result<uint32>` | Receive 1 to `size` bytes. Value: bytes received | `noInit`, `invalidParameter`, `timeout`, `connectionClosed`, `readError` |
| `Write<DataType>(const DataType& data)` | `ResultStatus` | Send the bytes of one object | As `WriteByteArray()` |
| `WriteArray<DataType>(const DataType *buffer, uint32 size)` | `ResultStatus` | Send `size` objects | As `WriteByteArray()` |
| `WriteString(const char *string)` | `ResultStatus` | Send a null-terminated string, without the terminator | As `WriteByteArray()` |
| `ReadExact(uint8 *buffer, uint32 size)` | `ResultStatus` | Read until `size` bytes are filled. Returns the first error from `ReadByteArray()` | As `ReadByteArray()` |

`Read()` returns `timeout` when nothing arrives within the timeout. It returns `connectionClosed` when the peer closes the connection. A timeout does not close the connection, so `Read()` can be called again.

`ReadExact()` may have consumed part of the buffer before it returns an error.

## ESP32 Port

`ASOCKET` is `SocketAdapterESP` (`Periphery/Adapter/Port/ESP32/Common/SocketAdapterESP.h`), built on the ESP-IDF lwIP sockets and `esp-tls`. Enabled by `#define VHAL_ESP32_SOCKET`.

Header: `#include <Adapter/Port/ESP32/Common/SocketAdapterESP.h>`

### Constructor

```cpp
SocketAdapterESP();
```

The copy constructor and copy assignment are deleted. The destructor closes the connection.

### Start Order

The ESP-IDF TCP/IP stack is started by the Wi-Fi adapter. Its `SetParameters()` initializes the driver and starts the stack (see [WiFiAdapter: SetParameters](WiFiAdapter.md#setparameters)). Call the Wi-Fi adapter's `SetParameters()` before any socket call. Connecting the link is a separate step and is not needed for the stack to start.

Until the stack is started, `Open()` and `Resolve()` return `noInit` and do not call lwIP. The adapter does not start the stack itself: ESP-IDF expects `esp_netif_init()` to be called once at application start, so starting it from a socket call would break that rule.

| Call without a started stack | Result |
|------------------------------|--------|
| `Open()` | `noInit` |
| `Resolve()` | `noInit` |

`Connect()` on the Wi-Fi adapter also returns `noInit` when `SetParameters()` was not called, and it does not start the stack.

### SetParameters

`SetParameters()` runs `beforePeripheryInit` (if set), closes any open connection, then runs `afterPeripheryInit` (if set). A non-`ok` result from the first hook is returned and nothing is closed.

### ESP-IDF Components

Add these to `REQUIRES` in the component `CMakeLists.txt`:

```cmake
REQUIRES lwip esp-tls mbedtls
```

### Behaviour

- Every connection is blocking. `SO_RCVTIMEO` and `SO_SNDTIMEO` are set to the timeout when the connection opens, so one `Read()` or `Write()` never waits longer than that.
- The adapter uses the `lwip_*` names on purpose, because the POSIX names may be redefined as macros by the lwIP configuration.
- Only IPv4 is used: name lookup is `AF_INET`.
- `Open()` connects with a non-blocking connect limited by the timeout, for `Tcp` and `Udp`. `Udp` sets the peer with `connect()` and sends and receives on that socket.
- A name lookup is not limited by the timeout. `Open()` with a name and `Resolve()` wait for the lwIP DNS client, which retries on its own timers: a few seconds with the lwIP defaults, longer if the DNS server is slow. A dotted IPv4 address is parsed without a DNS query.
- Without an IP address (link up but DHCP not finished, or no link), no DNS server is set. A name lookup then returns `notFound` at once. `Open()` with a dotted IPv4 address fails with `noConnection` when there is no route to the peer.
- `Read()` and `Write()` on `Tls` use the TLS connection. A `Write()` that times out on TLS is returned as `timeout` and is not retried, because the TLS state is then unknown.
- `Close()` is always `ok`.

### TLS Verification

`Tls` always verifies the server. `Parameters` has no option to skip verification, and the port does not disable it.

| `caCertificate` | Certificate used | Result when not available |
|-----------------|------------------|---------------------------|
| PEM text | The given CA certificate | Not applicable |
| `nullptr` | The built-in certificate bundle (`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE`) | `Open()` returns `notSupported` when the bundle is not enabled in `sdkconfig` |

A failed handshake, including a failed verification, is returned as `noConnection` from `Open()`. The port does not report the cause separately.

### Return Values

| Method | Status | When |
|--------|--------|------|
| `Open()` | `noInit` | TCP/IP stack not started. See [Start Order](#start-order) |
| | `invalidParameter` | `host` is `nullptr` or empty |
| | `notFound` | Name lookup failed (`Tcp` and `Udp`) |
| | `noConnection` | Socket could not be created, `Tcp` connect refused or failed, socket timeout could not be set, or TLS handshake failed (including verification) |
| | `timeout` | `Tcp` connect did not complete within the timeout |
| | `notSupported` | `Tls` with `caCertificate` `nullptr` and no certificate bundle |
| | `outOfMemory` | `Tls` could not allocate the TLS connection |
| `Resolve()` | `noInit` | TCP/IP stack not started. See [Start Order](#start-order) |
| | `invalidParameter` | `host` is `nullptr` or empty |
| | `notFound` | Lookup failed or returned no IPv4 address |
| `Read()` | `noInit` | No connection open |
| | `invalidParameter` | `size` is `0` |
| | `connectionClosed` | Peer closed the connection |
| | `timeout` | No data within the timeout (`EAGAIN`, or TLS wants read or write) |
| | `readError` | Any other error |
| `Write()` | `noInit` | No connection open |
| | `timeout` | Nothing could be sent within the timeout (`EAGAIN`, or TLS wants read or write) |
| | `writeError` | Any other error, or nothing was accepted |

## BSP Configuration Example

```cpp
// BSP.h
class BSP {
public:
    static ASOCKET httpClient;
};

// BSP.cpp
ASOCKET BSP::httpClient;
```

The member is not named `socket`: the port header notes that the POSIX names may be redefined as macros by the lwIP configuration.

## Usage Example

A function that works on any `SocketAdapter&`. It sends an HTTP/1.0 GET and copies the reply into `reply`. The server closes the connection at the end of an HTTP/1.0 reply, which is reported as `connectionClosed`. That is the normal end of the reply, not an error.

```cpp
#include <Adapter/SocketAdapter.h>
#include <cstdio>


// Returns ok when the reply was read to its end. received is the number of bytes copied.
ResultStatus HttpGet(SocketAdapter& client, uint16 port, const char *host, const char *path,
                     uint8 *reply, uint32 replySize, uint32& received) {
    received = 0;

    ResultStatus status = client.Open(host, port);
    if (status != ResultStatus::ok) {
        return status;
    }

    char request[256];
    snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", path, host);

    status = client.WriteString(request);
    if (status != ResultStatus::ok) {
        client.Close();
        return status;
    }

    while (received < replySize) {
        Result<uint32> result = client.ReadByteArray(reply + received, replySize - received);
        if (result.IsErr()) {
            client.Close();
            return (result.Error() == ResultStatus::connectionClosed) ? ResultStatus::ok : result.Error();
        }
        received += result.Value();
    }

    client.Close();
    return ResultStatus::ok;
}
```

Plain HTTP over the Wi-Fi station, with the link already `Ready`:

```cpp
uint8 reply[1024];
uint32 received = 0;

ResultStatus status = HttpGet(BSP::httpClient, 80, "example.com", "/", reply, sizeof(reply), received);
```

TLS, with a CA certificate given in PEM text. Only the set-up differs: the function is the same.

```cpp
static const char caPem[] =
    "-----BEGIN CERTIFICATE-----\n"
    "...\n"
    "-----END CERTIFICATE-----\n";

BSP::httpClient.SetParameters({
    .protocol = ASOCKET::Protocol::Tls,
    .caCertificate = caPem
});

uint8 reply[1024];
uint32 received = 0;

ResultStatus status = HttpGet(BSP::httpClient, 443, "example.com", "/", reply, sizeof(reply), received);
if (status == ResultStatus::noConnection) {
    // Connection or handshake failed, including a certificate that does not verify
}
```

The `caPem` array is static, so it stays valid until `Open()` returns, as required by `caCertificate`.
