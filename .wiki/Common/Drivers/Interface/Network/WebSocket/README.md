# WebSocketClient

Portable WebSocket client (RFC 6455, client side) on top of a [SocketAdapter](../../../../../Periphery/Adapter/SocketAdapter.md). `ws://` uses a TCP socket and `wss://` a TLS socket, so the same code runs on an ESP32, an STM32 or a laptop test host. It has no dependency on ESP-IDF.

The client does one job: connect, send and receive WebSocket messages, and close the connection correctly. It does not reconnect, does not follow redirects, and does not negotiate extensions.

Header: `#include <Drivers/Interface/Network/WebSocket/WebSocketClient.h>`

The class is header-only. It needs `SocketAdapter`, [SHA1](../../../../Utilities/Crypto/SHA/SHA1.md), [Base64](../../../../Utilities/Encoding/Base/Base64.md) and the `VSTD` allocator.

## Table of Contents

- [Overview](#overview)
- [Construction and Options](#construction-and-options)
- [API](#api)
- [Statuses](#statuses)
- [Callbacks and Views](#callbacks-and-views)
- [Memory and the Allocator](#memory-and-the-allocator)
- [Threading](#threading)
- [Example: Streaming Speech-to-Text](#example-streaming-speech-to-text)
- [Behaviour](#behaviour)
- [Limits](#limits)
- [References](#references)
- [Tests](#tests)

## Overview

```
Connect(url, options)  --> TCP or TLS, Open(), handshake (HTTP/1.1 Upgrade), state = open
Send*(...)             --> masked client frames (RFC 6455 5.3), fragmented when fragmentSize > 0
Poll(timeoutMs)        --> reads, unmasks, reassembles, answers pings, dispatches callbacks
Close(code, reason)    --> close frame, wait for the peer's close frame, Close() of the socket, onClose
```

One task drives the client. There are no background threads and no timers: everything happens inside `Connect`, the `Send*` calls, `Poll` and `Close`.

## Construction and Options

```cpp
WebSocketClient(SocketAdapter& adapter, VSTD::IAllocator& memory);
```

`adapter` carries the bytes. `memory` holds reassembled messages. Both must outlive the client. The client is not copyable.

`ConnectOptions`:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `headers` | `std::span<const Header>` | empty | Extra request headers, for example `{ "Authorization", "Bearer ..." }`. `Host`, `Upgrade`, `Connection`, `Sec-WebSocket-Version`, `Sec-WebSocket-Key`, `Sec-WebSocket-Protocol`, `Sec-WebSocket-Extensions` and `Sec-WebSocket-Accept` are rejected. Values may not contain CR, LF or control characters |
| `subprotocols` | `std::span<const std::string_view>` | empty | Offered in `Sec-WebSocket-Protocol`, most preferred first. The server must pick one of them, or none |
| `randomSource` | `std::function<uint32()>` | empty | **Required.** Returns 32 random bits per call. Used for the handshake key (4 calls) and for every frame's mask (1 call). See [Random source](#random-source) |
| `clockMs` | `std::function<uint32()>` | empty | **Required.** Milliseconds from a monotonic clock, for example `System::GetMs()`. Used for the handshake deadline, the close deadline and `Poll` |
| `handshakeTimeoutMs` | `uint32` | `10000` | Limit for the whole handshake, from the request to the end of a rejected body |
| `closeTimeoutMs` | `uint32` | `5000` | How long `Close()` waits for the peer's close frame |
| `maxMessageSize` | `uint32` | `65536` | Largest incoming message, in payload bytes. A larger one closes the connection with 1009. Also the largest message the reassembly buffer can grow to |
| `fragmentSize` | `uint32` | `0` | Outgoing messages are split into frames of at most this payload size. `0` sends one frame per message |
| `maxRejectBodyBytes` | `uint32` | `256` | Body bytes kept for `GetRejectBody()` after a non-101 answer. Clamped to 512 |

The `headers` and `subprotocols` spans are read during `Connect()` only. Their data must stay valid until `Connect()` returns.

### Random source

The handshake key and every frame mask come from `randomSource`. RFC 6455 section 10.3 requires the mask to be unpredictable to an intermediary, and a predictable key lets an attacker forge a handshake. Pass a source of real entropy, for example `esp_random()` on the ESP32 (with the radio or the SAR ADC enabled, as ESP-IDF documents). Do not use a PRNG seeded from the clock.

```cpp
options.randomSource = []() {
    return static_cast<uint32>(esp_random());
};
```

## API

| Method | Return | Description |
|--------|--------|-------------|
| `Connect(const char* url, const ConnectOptions& options)` | `ResultStatus` | Parse `ws://` or `wss://`, open the socket and perform the opening handshake. On `ok` the client is open. Sets the socket protocol to `Tcp` (`ws://`) or `Tls` (`wss://`), keeping its CA certificate |
| `SendText(std::string_view text)` | `ResultStatus` | Send a text message. The text must be valid UTF-8 |
| `SendBinary(std::span<const uint8> data)` | `ResultStatus` | Send a binary message |
| `SendPing(std::span<const uint8> payload)` | `ResultStatus` | Send a ping with at most 125 bytes. The peer's pong is handled by `Poll()` |
| `Close(uint16 code = 1000, std::string_view reason = {})` | `ResultStatus` | Send a close frame and wait up to `closeTimeoutMs` for the peer's close frame. `code` 0 sends an empty close payload. Always closes the socket. Calling it on a closed client returns `ok` |
| `Poll(uint32 timeoutMs)` | `ResultStatus` | Read what is available and dispatch complete messages, pings and close frames. See [Poll](#poll) |
| `IsOpen()` | `bool` | `true` while the connection is open for sending and receiving. `false` while closing and after the connection ended |
| `GetCloseCode()` | `uint16` | `0` until the connection closes; then the close code, see [Close codes](#close-codes) |
| `GetCloseReason()` | `std::string_view` | The reason the peer sent (or the empty string). Valid until the next `Connect()` |
| `GetHandshakeStatus()` | `uint16` | HTTP status of the handshake answer (`101` when open), `0` before any answer |
| `GetRejectBody()` | `std::string_view` | Up to `maxRejectBodyBytes` of the body of a non-101 answer, de-chunked. Valid until the next `Connect()` |
| `GetSubprotocol()` | `std::string_view` | The subprotocol the server chose, empty if none |

Callbacks are public fields:

| Field | Signature | Called when |
|-------|-----------|-------------|
| `onText` | `std::function<void(std::string_view text)>` | A complete text message arrived |
| `onBinary` | `std::function<void(std::span<const uint8> data)>` | A complete binary message arrived |
| `onClose` | `std::function<void(uint16 code, std::string_view reason)>` | The connection ended, once per connection: after a close handshake, a protocol error, or a lost socket |

### Poll

`Poll(timeoutMs)` processes data until `timeoutMs` has passed, or until a read times out with nothing new.

- The first socket read blocks for at most the socket's own timeout (`SocketAdapter::SetTimeout()`, which takes effect on the next `Open()`). `Poll` cannot change that timeout per call. Set the socket timeout to the largest wait you accept, for example `SetTimeout(20)` for a 100 ms audio pace.
- After the first read, `Poll` keeps reading while data arrives until `timeoutMs` has passed. A `timeoutMs` shorter than the socket timeout can therefore take up to one socket timeout.
- A read that times out with nothing received returns `timeout` and leaves the connection open.

### Close codes

`GetCloseCode()` and `onClose` report:

| Code | When |
|------|------|
| The peer's code | The peer sent a close frame |
| `1005` | The peer's close frame had no status code |
| `1006` | No close frame: the socket ended, failed, or the peer did not answer `Close()` in time |
| The code this side sent | A protocol error: `1002` (protocol error), `1007` (invalid UTF-8), `1009` (message too large), `1011` (out of memory while reassembling) |

Codes this side sends: `1000`-`1003`, `1007`-`1014`, `3000`-`4999`. Codes received: the same set. `1004`, `1005`, `1006` and `1015` are never sent, and a close frame carrying them is a protocol error.

## Statuses

| Method | Status | When |
|--------|--------|------|
| `Connect()` | `ok` | Connected, handshake answered with 101 and valid headers |
| | `busy` | The client is already open or closing |
| | `invalidParameter` | `url` is null or not `ws://` / `wss://`; empty host; bad port; `#` or `@` in the URL; URL or path too long; invalid header name or value; reserved header given; a subprotocol that is not a token; no `randomSource` or `clockMs` |
| | `notSupported` | IPv6 literal (`[...]` host) |
| | `noInit` / `notFound` / `noConnection` / `notSupported` | From `SocketAdapter::Open()`. For `wss://` on a socket without TLS (the host test socket), `notSupported` |
| | `timeout` | No complete answer within `handshakeTimeoutMs` |
| | `connectionClosed` / `readError` | The socket ended or failed during the handshake |
| | `invalidData` | Malformed status line, a header block with obsolete line folding or no colon, or an answer larger than the 1024-byte receive buffer |
| | `protocolError` | Valid HTTP but not a valid WebSocket answer: wrong `Upgrade` or `Connection`, `Sec-WebSocket-Accept` missing, duplicated or wrong, `Sec-WebSocket-Extensions` present, subprotocol not offered, more than one subprotocol header, or any non-101 status other than 401 and 403 (redirects are not followed) |
| | `authenticationFailed` | Status 401. Read `GetHandshakeStatus()` and `GetRejectBody()` |
| | `authorizationFailed` | Status 403. Read `GetRejectBody()` |
| | `writeError` | Socket write failed during the request |
| `SendText()` / `SendBinary()` / `SendPing()` | `ok` | Frame written |
| | `noConnection` | Not open (never connected, closing, or closed) |
| | `invalidParameter` | Text not valid UTF-8, or ping payload longer than 125 bytes |
| | `writeError` / `timeout` | Socket write failed. The connection is then closed (`onClose` with 1006), because a partly written frame cannot be recovered |
| `Close()` | `ok` | The peer's close frame arrived; the socket is closed |
| | `timeout` | No close frame within `closeTimeoutMs`. The socket is closed anyway, `onClose` reports 1006 |
| | `connectionClosed` / `readError` | The socket ended before the peer's close frame. The socket is closed |
| | `invalidParameter` | Code not sendable, reason longer than 123 bytes or not valid UTF-8, or a reason with code 0. Nothing is sent |
| `Poll()` | `ok` | Something was processed |
| | `timeout` | Nothing arrived within the first socket read. The connection stays open |
| | `connectionClosed` | The connection ended by a close frame (`onClose` has the code) |
| | `readError` | The socket failed. `onClose` reports 1006 |
| | `messageTooLong` | Incoming message above `maxMessageSize`. Closed with 1009 |
| | `protocolError` | Protocol violation. Closed with 1002 |
| | `invalidData` | Invalid UTF-8 in a text message, or in a close reason. Closed with 1007 |
| | `outOfMemory` | The allocator could not hold the message. Closed with 1011 |
| | `noConnection` | Not open |

A protocol error sends the close frame first and then closes the socket without waiting for the peer (RFC 6455 section 7.1.7).

## Callbacks and Views

- `std::string_view` and `std::span` passed to `onText` and `onBinary` point into the reassembly buffer. They are valid only during the call. Copy what you keep.
- `GetCloseReason()` and the `onClose` reason view are valid until the next `Connect()`.
- Callbacks may call the `Send*` functions. They must not call `Poll()`, `Connect()` or `Close()`. To stop from a callback, set a flag and call `Close()` after `Poll()` returns.
- `Close()` dispatches messages that arrive while it waits, so no data is lost.

## Memory and the Allocator

Only message reassembly uses dynamic memory, through the `VSTD::IAllocator&` given to the constructor. Everything else is a fixed member.

| Buffer | Size | Holds |
|--------|------|-------|
| receive | 1024 | Handshake answer, then frame bytes |
| transmit | 256 | Request and frames, flushed when full |
| control | 125 | Ping, pong and close payloads |
| message | `maxMessageSize` at most | One reassembled message, allocated with `Allocate` and grown with `Reallocate` (doubling, capped at `maxMessageSize`) |

**Handles can move.** The `Heap` in `VSTD` can defragment (`Defragment()`, `DefragStep()`), which moves blocks and updates the `Handle`. The client keeps the message as a `Handle`, never as a raw pointer, and resolves `Data()` again for each copy. An application that keeps data across calls must do the same.

Rules for the application:

- Call `Defragment()` between `Poll()` calls, never from inside `onText`, `onBinary` or `onClose`. A callback's view points into a block that a defragmentation step can move.
- A message that is still being reassembled survives a `Defragment()` between `Poll()` calls: the client holds the handle, not the address.
- `Connect()` copies the `randomSource` and `clockMs` functions into the client. A callable whose captures do not fit the `std::function` small buffer allocates when it is copied, so give it small captures, or none.
- The allocator must have room for `maxMessageSize` when a large message arrives. A `Heap` that is too small returns `outOfMemory` and the connection closes with 1011.

```cpp
VSTD::Heap<VSTD::IncrementalDefrag<2>> heap(32768);
WebSocketClient client(socket, heap);
```

`PoolAllocator` works too, with a block size of at least `maxMessageSize`. Its `Reallocate` cannot grow past one block, so a message larger than a block returns `outOfMemory`.

## Threading

Not thread-safe. Use one task for `Connect`, `Send*`, `Poll`, `Close` and the getters. A second task may not call them. The callbacks run on the task that calls `Poll()` or `Close()`.

## Example: Streaming Speech-to-Text

Connects to the xAI streaming transcription endpoint with a bearer token, sends 100 ms of 16 kHz PCM16 mono audio (3200 bytes) per binary frame, and reads the `transcript.partial` events between frames.

```cpp
#include <Drivers/Interface/Network/WebSocket/WebSocketClient.h>
#include <Utilities/VSTD/memory/Heap.h>
#include <esp_random.h>
#include <algorithm>


static ASOCKET sttSocket;
static VSTD::Heap<VSTD::IncrementalDefrag<2>> sttHeap(32768);
static WebSocketClient stt(sttSocket, sttHeap);
static constexpr size_t kFrameBytes = 3200; // 100 ms of 16 kHz PCM16 mono


static void OnText(std::string_view json) {
    // The view is valid only during this call: copy anything that must outlive it.
    if (json.find("\"transcript.partial\"") != std::string_view::npos) {
        // Parse "text" and "speech_final" here.
    }
}


static void OnClose(uint16 code, std::string_view reason) {
    // Normal end is 1000. 1006 means the socket was lost.
}


// authorization is the full header value, "Bearer <api key>". On failure, GetRejectBody() holds the server's JSON error.
ResultStatus OpenTranscription(std::string_view authorization) {
    static const WebSocketClient::Header headers[] = { { "Authorization", authorization } };

    stt.onText = OnText;
    stt.onClose = OnClose;

    sttSocket.SetTimeout(20); // one socket read blocks at most 20 ms, so Poll stays close to the 100 ms pace

    WebSocketClient::ConnectOptions options;
    options.headers = headers;
    options.randomSource = []() { return static_cast<uint32>(esp_random()); };
    options.clockMs = []() { return static_cast<uint32>(System::GetMs()); };

    return stt.Connect("wss://api.x.ai/v1/stt?model=grok-voice-transcribe-2.0&encoding=pcm"
                       "&sample_rate=16000&interim_results=true&language=en", options);
}


// Sends the clip one 100 ms frame at a time. The BSP's timer paces the frames; Poll drains events between them.
ResultStatus StreamClip(const uint8* pcm, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        size_t count = std::min(kFrameBytes, size - offset);
        ResultStatus status = stt.SendBinary(std::span<const uint8>(pcm + offset, count));
        if (status != ResultStatus::ok) {
            return status;
        }

        offset += count;
        stt.Poll(0);
        // Wait here for the BSP's 100 ms tick.
    }

    stt.SendText("{\"type\":\"audio.done\"}");

    // Bounded drain: the server closes after transcript.done.
    for (uint32 attempt = 0; attempt < 20 && stt.IsOpen(); attempt++) {
        stt.Poll(500);
    }

    return stt.IsOpen() ? ResultStatus::timeout : ResultStatus::ok;
}
```

If `OpenTranscription()` returns `authenticationFailed` or `authorizationFailed`, show `stt.GetRejectBody()` (the server's JSON error) to the user before you retry.

## Behaviour

- Handshake: sends `GET <path> HTTP/1.1` with `Host` (the port is included when it is not the default), `Upgrade: websocket`, `Connection: Upgrade`, `Sec-WebSocket-Version: 13`, the key, the offered subprotocols and the extra headers. Accepts only status 101 (or 401 and 403, which are reported with their body). Checks the `Upgrade` and `Connection` token lists case-insensitively, exactly one `Sec-WebSocket-Accept`, and the chosen subprotocol against the offered list.
- Bytes that follow the handshake answer in the same read are frame data and are processed first.
- Frames: client frames are masked with a fresh key for each frame. Lengths use the minimal encoding (7, 16 or 64 bits).
- Ping: answered with a pong carrying the same payload, even during `Close()`. An unsolicited pong is ignored.
- Fragments: a data message may be fragmented, and control frames may come between the fragments. Text is validated as UTF-8 across fragments as it arrives.
- Sending a message that is larger than `fragmentSize` splits it into a first frame and continuation frames, with FIN on the last.
- Close: `Close()` sends the close frame and waits for the peer's close frame. A close frame from the peer is answered with a close frame that echoes its status code (an empty answer when the peer sent none), and then the socket is closed.
- Keepalive: the client sends no pings on its own. The application calls `SendPing()` if it needs a keepalive.

## Limits

- Client side of RFC 6455 only. No extensions (`permessage-deflate` is not offered, and a server that accepts one fails the handshake).
- No redirects. A 3xx answer returns `protocolError`.
- No IPv6 literals. `[::1]` returns `notSupported`.
- `wss://` needs a socket with TLS. The ESP32 port provides it; the laptop test socket does not.
- No automatic reconnect and no keepalive timer.
- Not thread-safe. One task.
- Handshake answer up to 1024 bytes (headers). Rejected body kept up to 512 bytes. Host up to 127 characters, path and query up to 255.
- `maxMessageSize` is the reassembly limit for incoming messages. The client does not limit outgoing message sizes; the peer does.
- `randomSource` and `clockMs` are required. Without them `Connect()` returns `invalidParameter`.
- A non-101 answer with neither `Content-Length` nor chunked encoding is read until the connection closes or the handshake deadline passes. Servers that keep the connection open for an error answer therefore delay `Connect()` by up to `handshakeTimeoutMs`.
- The socket timeout bounds each blocking read, so `Poll` and the close wait can overrun their own timeout by up to one socket timeout.

## References

The sources studied, and the edge cases taken from each. Sources were read on 2026-11-18.

### RFC 6455 (The WebSocket Protocol)

Sections 4.1 (client requirements), 4.2.1 (what the client must verify in the answer), 5.1 to 5.5 (framing, masking, fragmentation, control frames), 7.1.5 and 7.1.7 (close codes, failing the connection), 7.4 (status codes) and 8.1 (UTF-8).

Taken:
- Mask bit set on a server frame: fail with 1002 (section 5.1).
- Reserved bits RSV1 to RSV3 set with no extension: fail with 1002 (5.2).
- Opcodes 3 to 7 and 11 to 15: fail with 1002 (5.2).
- Minimal length encoding: 126 only for 126 to 65535, 127 only above 65535, and the 64-bit MSB clear. Non-minimal encodings fail with 1002 (5.2).
- A fresh, unpredictable 4-byte mask for each client frame (5.3).
- Fragmented messages, with control frames allowed between fragments and never fragmented (5.4, 5.5).
- Control frames: payload at most 125 bytes, FIN set (5.5). A ping is answered with a pong carrying the same payload. An unsolicited pong is ignored (5.5.2, 5.5.3).
- Close payload: empty, or a 2-byte code followed by a UTF-8 reason. A one-byte payload is a protocol error (5.5.1).
- No data frames after sending a close frame (5.5.1).
- A received close frame is answered with a close frame (5.5.1).
- Codes 1004, 1005, 1006 and 1015 are never sent (7.4.1). Unknown codes are rejected (7.4.2).
- Fail the connection: send a close frame with a suitable code, then close the TCP connection, without processing more of the peer's data (7.1.7).
- Invalid UTF-8 in a text message, checked across fragments, gives 1007 (8.1).
- Handshake: the answer must be 101, `Upgrade` must be `websocket`, `Connection` must contain `upgrade`, and `Sec-WebSocket-Accept` must match. An extension or subprotocol the client did not offer fails the connection (4.1).

### ESP-IDF `esp_websocket_client` (espressif/esp_websocket_client, with ESP-IDF `transport_ws.c`)

Taken:
- Handshake answer read until the blank line, with partial reads handled and a size bound (`transport_ws.c`).
- Bytes after the handshake answer are kept and parsed as frame data (`transport_ws.c`, the leftover-buffer handling).
- Duplicated `Sec-WebSocket-Accept` fails. The accept value is recomputed and compared.
- Reserved RSV bits fail; reserved opcodes fail; fragmented control frames fail; control payload over 125 fails.
- A continuation with no message in progress fails, and a new data frame during a fragmented message fails.
- A 64-bit length with the high bit set fails.
- Ping answered inside the transport with the same payload.

Not taken, and why:
- Accepting any status that carries a valid accept value. We require 101 (see the conflicts below).
- Following 301 to 308 redirects. The client returns `protocolError` instead, because a redirect would send the bearer token to another host.
- Empty close echo. We echo the code, as RFC 6455 suggests.
- A ping every 10 seconds and an abort after 120 seconds without a pong. The application decides the keepalive (`SendPing`).
- Auto-reconnect. Not in scope.
- Masked server frames are read and accepted by ESP-IDF. We fail them with 1002.

### wslay (tatsuhiro-t/wslay)

Taken:
- Incoming frames with the mask bit set fail with 1002 (client side).
- Control frames longer than 125 bytes, or with FIN clear, fail (`wslay_frame.c`).
- Minimal length encoding is enforced. wslay sends an empty close for this error. We send 1002, as RFC 6455 7.1.7 asks.
- UTF-8 validated as a state machine kept across fragments and checked at FIN. Ours is incremental, so it fails at the first bad byte.
- Close codes are checked against the valid set, with 1004, 1005 and 1006 excluded and 3000 to 4999 allowed.
- A close frame is answered with a close frame carrying the same code.
- The message size limit fails with 1009.

Not taken, and why:
- Accepting a one-byte close payload as "no status". RFC 6455 requires a 2-byte code, so we fail with 1002.
- Rejecting 1012 to 1014. These are IANA-registered and the Python `websockets` library accepts them, so we accept them.
- A message size limit of about 2 GiB. We use 64 KiB by default, because the buffer lives in an MCU heap.

### Python websockets (python-websockets/websockets)

Taken:
- Handshake checks: `Connection` token, `Upgrade` equal to `websocket` case-insensitively, exactly one matching `Sec-WebSocket-Accept`, no unoffered extension or subprotocol.
- Non-101 answers raise an error that carries the status and body (sync client). We return `authenticationFailed` or `authorizationFailed` for 401 and 403, and keep the body.
- Masked server frame: 1002. RSV bits: 1002. Control payload over 125 or FIN clear: 1002. One-byte close payload: 1002.
- Invalid UTF-8 in the close reason: 1007.
- Close code set: 1000 to 1003, 1007 to 1014, and 3000 to 4999. Codes 1004, 1005, 1006 and 1015 are never sent.
- Close echo and the empty close: an empty close stays empty.
- Fail sends a close frame only when the connection is open, and never sends 1006.
- Max message size with a 1009 close. Default 1 MiB in the library; ours is 64 KiB.
- Close handshake with a timeout (`close_timeout`, 10 s in the library; ours is 5 s).
- Pong answering with the same data, and an unsolicited pong ignored.

Not taken, and why:
- The library's 1 MiB default message limit: too large for an MCU heap.
- Automatic keepalive pings (`ping_interval`): the application decides.

### Conflicts between the sources, and what we chose

| Question | Sources | Our choice |
|----------|---------|------------|
| Answer status | RFC: follow HTTP, redirects optional. ESP-IDF: accepts any status with a valid accept. websockets sync: requires 101 | Require 101. 401 and 403 are reported with the body |
| Minimal length encoding | RFC: MUST. wslay: rejects. ESP-IDF: not checked | Reject with 1002 |
| Codes 1012 to 1014 | RFC: not listed. websockets: accepts. wslay: rejects | Accept |
| Close echo | RFC: typically echoes the code. wslay, websockets: echo. ESP-IDF: empty | Echo the code |
| Message size | RFC: no limit. websockets: 1 MiB. ESP-IDF: streams, no limit | 64 KiB by default, configurable |
| Frame-level error code | RFC: send a suitable code. wslay: sometimes an empty close. websockets: 1002 | Always 1002 for framing errors |

## Tests

`tools/vhal-host-tests/websocket/run.sh` builds and runs three suites, plain and under ASan and UBSan:

- `sha1_tests.cpp`: SHA-1 vectors and the RFC 6455 accept example.
- `frame_tests.cpp`: byte-exact frames against a scripted socket (`scripted_socket.h`). Covers the handshake, masks, lengths 0 to 65536, fragments, control frames, close handshake, every protocol error listed above, send-side fragmentation and a defragmenting `Heap`.
- `live_tests.cpp`: the client over real TCP (`posix_socket.h`) against `ws_server.py`, which uses the `websockets` library as the reference peer. Covers echo, the server's ping, server close codes, fragments both ways, 401 and 403 bodies, the authorization header, subprotocols, the 64 KiB limit, a paced stream with polling, and defragmentation between messages.

Compile check on the ESP32 port: `tools/vhal-harness/main/checks/CheckWebSocket.cpp`, run by `idf.py build` (see `tools/vhal-harness/README.md`).
