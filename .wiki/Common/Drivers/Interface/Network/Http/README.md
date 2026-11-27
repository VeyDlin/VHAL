# HTTP: HttpClient and ServerSentEvents

Portable HTTP/1.0 and HTTP/1.1 client, and a Server-Sent Events (SSE) parser. Both are in `Common/Drivers/Interface/Network/Http/` and are header-only. They use only the `SocketAdapter&` interface and an `IAllocator&` for dynamic memory. They do not include any ESP-IDF or MCU header, so they compile for ESP32, STM32 and nRF.

- `HttpClient.h`: request builder, response parser, streamed body, keep-alive reuse.
- `ServerSentEvents.h`: transport-agnostic SSE parser, fed with byte chunks.
- `HttpByteBuffer.h`: internal growable byte block used by both classes. Not part of the API.

Headers: `#include <Drivers/Interface/Network/Http/HttpClient.h>` and `#include <Drivers/Interface/Network/Http/ServerSentEvents.h>`

`https://` is TLS through the socket port: the client sets `SocketAdapter::Protocol::Tls` and passes `Options::caCertificate`. The socket must be up before `Send()`; see [SocketAdapter](../../../../../Periphery/Adapter/SocketAdapter.md).

## Scope

| In | Out |
|----|-----|
| Absolute `http://` and `https://` URLs, host, port, path and query | IPv6 literals, userinfo (`user:pass@`), fragments are dropped |
| Methods GET, HEAD, POST, PUT, PATCH, DELETE | Other methods |
| Request headers from the caller, `Content-Length` written for the body | `Host`, `Content-Length`, `Transfer-Encoding`, `Connection` from the caller: refused |
| Status line and headers, HTTP/1.0 and HTTP/1.1 responses | HTTP/2 and later |
| Body by `Content-Length`, `Transfer-Encoding: chunked`, or read to close | Compression (`Content-Encoding` is passed through as bytes) |
| 1xx interim responses skipped | Upgrades (101 is returned as a final response with no body) |
| 3xx returned to the caller with `Location` | Redirects are not followed |
| Keep-alive reuse for the same scheme, host and port | Pipelining, connection pools |

## HttpClient

Header: `#include <Drivers/Interface/Network/Http/HttpClient.h>`

Constructor: `HttpClient(SocketAdapter& adapter, Memory::IAllocator& memory)`. The client holds both references: they must outlive it. The destructor closes the connection.

### Enums and types

| Type | Values | Description |
|------|--------|-------------|
| `Method` | `Get`, `Head`, `Post`, `Put`, `Patch`, `Delete` | Request method |
| `Scheme` | `Http`, `Https` | Parsed URL scheme |
| `Header` | `std::string_view name`, `std::string_view value` | One request header |
| `Request` | `Method method`, `std::string_view url`, `std::span<const Header> headers`, `std::span<const uint8> body` | Everything `Send()` needs. Views must stay valid until `Send()` returns |
| `UrlParts` | `Scheme scheme`, `std::string_view host`, `uint16 port`, `std::string_view target` | Output of `ParseUrl()`. Views point into the URL. An empty `target` means `/` |
| `Options` | `uint32 connectTimeoutMs = 10000`, `uint32 timeoutMs = 10000`, `uint32 maxHeaderBytes = 4096`, `const char *caCertificate = nullptr`, `const char *userAgent = "VHAL-HttpClient/1.0"` | Public field `options`. Change it between requests |

### API

| Method | Return | Description | Errors |
|--------|--------|-------------|--------|
| `static ParseUrl(std::string_view url, UrlParts& parts)` | `ResultStatus` | Split an absolute `http://` or `https://` URL. Default port 80 or 443. Rejects spaces and control characters | `invalidParameter`: no scheme, empty host, bad port (0 or above 65535), bad host character, space in target. `notSupported`: other scheme, userinfo, IPv6 literal. `outOfRange`: host of 128 bytes or more |
| `Send(const Request& request)` | `ResultStatus` | Connect if needed, write the request, read the final response head. Interim 1xx responses are skipped. Afterwards `GetStatusCode()` and `FindHeader()` are valid and the body is pending | `invalidParameter`: bad header name or value (CR, LF or NUL in a value), a `Host`, `Content-Length`, `Transfer-Encoding` or `Connection` header from the caller, a body on `Head`. `outOfRange`: body above 4 GiB-1, or header block above `maxHeaderBytes`. `invalidData`: malformed status line, header without colon, whitespace before colon, obs-fold, `Content-Length` values that disagree or are not digits, `Transfer-Encoding` with `Content-Length`. `notSupported`: `HTTP/2` or other major version, plus any socket `Open()` status such as `notSupported` for `https` without TLS. `noConnection`, `timeout`, `notFound`, `readError`, `writeError`, `connectionClosed`: from the socket. `outOfMemory`: the heap could not hold the header block |
| `ReadBody(Callback::Ref<ResultStatus(std::span<const uint8>)> onBody)` | `ResultStatus` | Stream the rest of the body to `onBody`, which is called with each piece and returns `ok` to continue. The span is valid only during the call. Ends the response: the connection is kept for reuse when the response allows it, otherwise closed | `ok` at the end of the body. `invalidParameter`: `onBody` is empty, and the body stays pending. `notReady`: no body pending (no `Send()`, or the body was already read). `connectionClosed`: the peer closed before a `Content-Length` or chunked body was complete. `invalidData`: bad chunk size, bad chunk framing (CRLF missing after data). `outOfRange`: trailer section above `maxHeaderBytes`. Any other status from the socket. The callback's non-`ok` status is returned as is and closes the connection |
| `ReadBodyTo(std::span<uint8> buffer, uint32& size)` | `ResultStatus` | Read the whole body into `buffer`. `size` receives the bytes stored | As `ReadBody()`. `outOfRange` also when the body is larger than `buffer`; the rest is discarded with the connection |
| `Close()` | `ResultStatus` | Close the connection and forget any unread body. Safe to call when nothing is open | `ok` or the socket's status |
| `GetStatusCode()` | `uint16` | Status code of the final response. 0 before the first `Send()` | None |
| `FindHeader(std::string_view name, std::string_view& value)` | `bool` | Look up a response header, ignoring case. The first occurrence wins; the value is trimmed | None. `value` is valid until the next `Send()`, `Close()`, or a heap defragment step |
| `IsConnected()` | `bool` | `true` while the socket is open | None |
| `ParseUrl()` | `ResultStatus` | See above | See above |

### Usage Rules

- Call `ReadBody()` or `ReadBodyTo()` after every successful `Send()` that you want to keep the connection for. A body that is not read is discarded: the next `Send()` closes the connection first.
- The response head of the last request stays available until the next `Send()`. `FindHeader()` works after the body is read.
- A `Send()` on a kept connection to the same scheme, host and port reuses the socket. A different endpoint closes it and connects again.

### Timeouts

Two limits, one per phase:

- `Options::connectTimeoutMs` (default `10000`) is the TCP connect and, for `https`, the TLS handshake. It is set as `SocketAdapter::Parameters::connectTimeoutMs` before `Open()`.
- `Options::timeoutMs` (default `10000`) is the limit for one read and one write. It is applied to the open connection with `SocketAdapter::SetIoTimeout()` before the request is written.

A first-byte or idle timeout as a separate value is not offered: a read that waits for the response uses `timeoutMs`. See [Limits](#limits).

### Keep-alive and Stale Connections

A response keeps the connection when all of these hold: the body length is known (`Content-Length`, chunked, or no body), `Connection: close` is absent, the response is not 101, and for HTTP/1.0 `Connection: keep-alive` is present. No bytes may follow the body in the same read, since that would be a protocol error.

A reused connection that the server closed while idle fails before any response byte arrives. In that case the client sends the request once more on a new connection, for `writeError`, `connectionClosed` and `readError`. A `timeout` is not retried. The retry means a POST body can be sent a second time only when the first attempt never reached the server; the first attempt's connection was already closed by the server.

### Body Length (RFC 9112 section 6.3)

| Condition | Body |
|-----------|------|
| `HEAD` request, 101, 204 or 304 | None |
| `Transfer-Encoding` with `chunked` as the last coding (HTTP/1.1) | Chunked |
| `Transfer-Encoding` without `chunked` last (HTTP/1.1) | Read to close |
| `Transfer-Encoding` and `Content-Length` together | Refused: `invalidData` |
| `Content-Length` (one value, or repeated or listed values that all agree) | That many bytes |
| Neither, or HTTP/1.0 with `Transfer-Encoding` | Read to close |

HTTP/1.0 responses ignore `Transfer-Encoding`, so they use `Content-Length` or read to close. The `Connection: close` case and the close-delimited body end the response only when the server closes.

### Chunked Coding

- Size is hexadecimal, up to 16 digits. More than that is `invalidData`, and so is an empty size line.
- Chunk extensions (`;name=value`) and whitespace before them are read to the end of the size line and ignored.
- Trailer fields are skipped. They count against `maxHeaderBytes`.
- Bare LF is accepted as a line end. A bare CR before the LF is ignored.

## ServerSentEvents

Header: `#include <Drivers/Interface/Network/Http/ServerSentEvents.h>`

Constructor: `ServerSentEvents(Memory::IAllocator& memory, uint32 maxEventBytes)`. The parser does not know about HTTP: feed it the bytes of any stream.

### API

| Member | Return | Description | Errors / Notes |
|--------|--------|-------------|----------------|
| `onEvent` | `Callback::Delegate<void(std::string_view type, std::string_view data, std::string_view id)>`. Bind a member with `Bind<T, &T::Method>(&object)`; a callback with captures does not fit | Called once per dispatched event. `type` is `"message"` when the event has no `event:` field. `id` is the last event ID, which persists across events | The views are valid only during the call |
| `Feed(std::span<const uint8> bytes)` | `ResultStatus` | Parse the next bytes. Any split is allowed, including inside a CRLF or a UTF-8 sequence | `ok`, or the reason of the last event dropped during this call: `outOfRange` when it exceeded `maxEventBytes`, `outOfMemory` when the heap could not hold it. A dropped event is not dispatched; the parser resyncs at the next blank line, so the caller keeps feeding |
| `Reset()` | `void` | Start a new stream: forget the last event ID, the retry value and any partial event | None |
| `GetLastEventId()` | `std::string_view` | The last event ID | Valid until the next `Feed()` or heap defragment step |
| `HasRetry()` | `bool` | `true` once a valid `retry:` field was seen | None |
| `GetRetry()` | `uint32` | Reconnection time in milliseconds. Saturates at `0xFFFFFFFF` | Valid when `HasRetry()` is true |

The parser ignores the `retry` value itself; reconnecting is the caller's job.

### Event Rules (WHATWG)

- Lines end with CRLF, LF or CR. A CR at the end of one `Feed()` and an LF at the start of the next are one line end.
- One UTF-8 byte order mark at the start of the stream is removed. Later BOM bytes are kept in the value.
- A line starting with `:` is a comment and is ignored.
- Text before the first colon is the field name. The value is the text after it, with one leading space removed. A line with no colon is a field with an empty value.
- `data` appends the value and a LF to the data buffer. `event` sets the event type. `id` sets the last event ID unless the value holds a NUL. `retry` sets the reconnection time when the value is only ASCII digits. Other names are ignored. Names are case-sensitive.
- A blank line dispatches the event. With an empty data buffer nothing is dispatched, and the event type is still cleared.
- The final LF of the data buffer is removed before dispatch. An event with no blank line at the end of the stream is discarded.
- Invalid UTF-8 is decoded with U+FFFD replacement, as the WHATWG decoder does. The sequence is split correctly across `Feed()` calls.

### Limits per Event

`maxEventBytes` bounds the event type plus the data of one event, and each of the type and the ID value on its own. When an event exceeds it, the event is dropped: data is discarded up to the next blank line, and no `onEvent` is called for it. An ID set by the dropped event still applies, as the WHATWG rules say.

## Memory Rules

- All dynamic memory comes from the `IAllocator&` given to the constructor. Blocks are `Handle`s, and the head buffer and the SSE buffers grow with `Reallocate()` up to their limits (`maxHeaderBytes`, `maxEventBytes`).
- The fixed buffers are members: 512 bytes to receive, 256 bytes to send, a 128-byte NUL-terminated host copy.
- **A block can move.** `Heap::Defragment()` moves live blocks, so the memory behind a `Handle` changes. The code therefore calls `Handle::Data()` again after every call that can allocate or run a heap step. No raw pointer or view is kept across a socket read.
- A view returned by `FindHeader()`, `GetLastEventId()` or passed to `onEvent` is valid until the next call into the client or parser, or until a defragment step. Copy the text if it is kept.
- Do not call `Defragment()` inside `onEvent` or a body callback passed to `ReadBody()`. Defragmenting from `SocketAdapter::ReadByteArray()` is fine: the client re-reads its handles afterwards.
- Set up a heap large enough for one header block and for the event buffers. A failed allocation is `outOfMemory` (header) or a dropped event (SSE), and the rest of the stream is still parsed.

## Example: xAI Chat Completions

POST a JSON body with `"stream": true` and print each `choices[0].delta.content`.

```cpp
#include <Drivers/Interface/Network/Http/HttpClient.h>
#include <Drivers/Interface/Network/Http/ServerSentEvents.h>
#include <Utilities/Serialization/JSON/JSON.h>
#include <Utilities/Memory/Heap.h>
#include <array>
#include <cstdio>


// Prints the answer as it arrives. Returns the status of the request.
ResultStatus StreamChat(SocketAdapter& socket, const char *apiKey) {
    static Memory::Heap<> heap(8192);

    HttpClient client(socket, heap);
    client.options.timeoutMs = 15000;
    client.options.caCertificate = nullptr;  // built-in bundle of the port

    char authorization[128];
    std::snprintf(authorization, sizeof(authorization), "Bearer %s", apiKey);

    static const HttpClient::Header headers[] = {
        { "Authorization", authorization },
        { "Content-Type", "application/json" },
    };
    static const char body[] =
        "{\"model\":\"grok-4.20-0309-non-reasoning\",\"stream\":true,"
        "\"messages\":[{\"role\":\"user\",\"content\":\"Hello\"}]}";

    HttpClient::Request request;
    request.method = HttpClient::Method::Post;
    request.url = "https://api.x.ai/v1/chat/completions";
    request.headers = headers;
    request.body = std::span<const uint8>(reinterpret_cast<const uint8 *>(body), sizeof(body) - 1);

    ResultStatus status = client.Send(request);
    if (status != ResultStatus::ok) {
        return status;
    }

    if (client.GetStatusCode() != 200) {
        client.Close();
        return ResultStatus::invalidData;
    }

    // The callbacks run during Feed(); onEvent gets one SSE data payload per call.
    ServerSentEvents events(heap, 4096);
    events.onEvent = [](std::string_view type, std::string_view data, std::string_view id) -> void {
        (void)type;
        (void)id;
        if (data == "[DONE]") {
            return;
        }

        std::string_view choice = JSON::GetArrayElement(JSON::GetRaw(data, "choices"), 0);
        std::string_view delta = JSON::GetRaw(choice, "delta");

        std::array<char, 256> text{};
        size_t textLength = 0;
        if (JSON::GetString(delta, "content", std::span(text), textLength)) {
            std::fwrite(text.data(), 1, textLength, stdout);
        }
    };

    return client.ReadBody([&events](std::span<const uint8> piece) -> ResultStatus {
        events.Feed(piece);  // a dropped event is reported by Feed(); the stream continues
        return ResultStatus::ok;
    });
}
```

Notes on the example:

- `ReadBody()` reads the chunked body; `Feed()` sees the SSE bytes as they arrive, so each token is printed when its packet arrives.
- `data == "[DONE]"` is the end marker; the last `ReadBody()` status is `ok` when the server finished the chunked body.
- The JSON calls are the nested-access functions of [JSON](../../../../Utilities/Serialization/JSON/JSON.md#nested-access-getraw-and-getarrayelement): `GetRaw()` for `choices` and `delta`, `GetArrayElement()` for index 0, `GetString()` for `content`. `"Grüße"` is UTF-8 text with multi-byte characters and arrives unchanged.
- The `static` heap here is only for the example. The application chooses its own heap; the one-time heap of this example is 8 KiB, large enough for a header block and the event buffers of this stream.
- For a stream with a longer event, raise the `ServerSentEvents` limit (here 4096) or the drop shows up as an `outOfRange` from `Feed()`.

## References

Edge cases taken from each source, and where each one lands in the code.

### RFC 9112 (HTTP/1.1 message syntax)

Read in this pass: section 2.2 (message parsing), section 6.1 (Transfer-Encoding with HTTP/1.0), section 6.3 (message body length, rules 1 to 7), section 7.1 (chunked), 7.1.1 (chunk extensions) and 7.1.2 (trailers).

- Section 2.2: ignore leading empty lines before a status line. Code: `ReadHeadBlock()` skips empty lines before the status line.
- Section 2.2: accept bare LF as a line end and ignore a preceding CR. Code: `ReadHeadBlock()`, `NextLine()`.
- Section 2.2: a bare CR outside content must be rejected. Code: header values with CR are refused (`IsFieldValue()`).
- Section 6.3 rule 1: HEAD, 1xx, 204, 304 have no body. Code: `ParseHeaders()`.
- Section 6.3 rule 3: `Transfer-Encoding` with `Content-Length` is an error. Code: `invalidData`, not a guess.
- Section 6.3 rule 4: chunked not final means read to close. Code: `UntilClose`.
- Section 6.3 rule 5: invalid or conflicting `Content-Length` is unrecoverable; a list of identical valid numbers is fine. Code: `AddContentLength()`.
- Section 6.3 rule 7: no length means read to close for a response. Code: `UntilClose` (rule 7 covers requests).
- Section 6.1: an HTTP/1.0 message with `Transfer-Encoding` is faulty. Deviation: the client ignores `Transfer-Encoding` for HTTP/1.0 and uses `Content-Length` or read to close, and does not close with an error. Documented in [Body Length](#body-length-rfc-9112-section-63).
- Section 7.1: chunk size is hexadecimal; chunk extensions are ignored (7.1.1 says recipients must ignore unknown extensions). Code: `ConsumeChunkFraming()`.
- Section 7.1.2: trailers may be discarded. Code: `ConsumeTrailerByte()`, with a size limit (7.1.1 says servers should limit extension length; the trailer limit follows the same idea).

### WHATWG HTML, "Server-sent events"

Read in this pass: the parsing section.

- Lines end with CRLF, LF or CR. Code: `Process()`, with `afterCarriageReturn` across `Feed()` calls.
- One leading BOM is removed. Code: `HandleBom()`, which replays bytes that only look like a BOM.
- Comment lines (`:`) are ignored. Code: a name with length 0 is `Field::Ignored`.
- One leading space after the colon is removed. Code: `skipSpace`.
- A line without a colon is a field with an empty value. Code: `EndLine()` classifies the name.
- `id` containing NUL is ignored. Code: `nulSeen`.
- `retry` must be digits only. Code: `AddRetryByte()`.
- Blank line dispatches; empty data is not dispatched; the last LF is removed; type defaults to `message`; an unterminated event is discarded. Code: `Dispatch()`.
- The last event ID persists across events. Code: `lastEventId`.
- Invalid UTF-8 becomes U+FFFD. The page does not say how, so the Encoding Standard's decoder rules are followed. Code: `DecodeByte()`, `StartSequence()`.

### llhttp (nodejs/llhttp)

The categories named in llhttp's README are the checklist, and the host tests cover each one: chunk extensions (`http-chunked-every-split`, `integ-chunked-extensions-and-trailers`), obs-fold (`http-obs-fold-refused`), conflicting `Content-Length` (`http-length-conflict-refused`), `Transfer-Encoding` with `Content-Length` (`http-length-and-chunked-refused`), and leading CRLF (`http-status-line-variants`). The cases are in `Common/Drivers/Interface/Network/Http/.test/TestHttpUnit.cpp` and `TestHttpIntegration.cpp`, run with `./test.cmd Http`.

### ESP-IDF `esp_http_client`

Read in this pass: `components/esp_http_client/esp_http_client.c`.

- Bodyless responses for HEAD, 204 and 304. Code: `ParseHeaders()`.
- Keep-alive decision at the end of a request: connection kept only when the response allows it. Code: `keepAlive`, `ReadBody()`.
- Reuse the transport when it is still connected; reconnect when the host or port changes. Code: `IsSameEndpoint()`, `Connect()`.
- A FIN before the response starts is a closed connection. Code: `connectionClosed` on a reused connection, with the retry.
- Timeouts apply per socket read and write, and a timeout is reported without wedging the parser. Code: the socket timeout, `timeout` status.

Differences from `esp_http_client`, kept on purpose:

- It follows redirects (301, 302, 303, 307, 308). This client does not: the brief requires the 3xx and `Location` to be returned.
- It applies TCP keep-alive probes. This client has no such option in `SocketAdapter`.
- Per-phase timeouts: it has separate connect and read timeouts through its transport. This client has a connect and an I/O timeout, and no first-byte or idle timeout; see [Limits](#limits).
- 1xx: `esp_http_client` has no explicit 1xx handling in the code read. This client skips 1xx responses other than 101.

## Limits

- **Connect and I/O timeouts are separate, but there is no first-byte or idle timeout.** `Options::connectTimeoutMs` bounds connect and the TLS handshake; `Options::timeoutMs` bounds each read and write. A separate first-byte timeout would need the client to change the read limit between the request and the first response byte.
- **HTTP/1.0 and HTTP/1.1 only**, and no HTTP/2.
- **No redirects**, no cookies, no authentication scheme beyond the `Authorization` header the caller sets.
- **No content decoding.** `gzip` and other `Content-Encoding` bodies are delivered as bytes.
- **Host names only** (IPv4 dotted or a name), up to 127 bytes. IPv6 literals are refused.
- **One request at a time** per client. `Send()` ends the previous response.
- **TLS** is whatever the socket port provides. The host tests use plain TCP, so `https` is tested only as `notSupported`.
- **Header block** (status line and headers) is limited by `maxHeaderBytes`; trailers by the same value.
- **Heap:** the header block and SSE buffers need a heap with room for them; see [Memory Rules](#memory-rules).
