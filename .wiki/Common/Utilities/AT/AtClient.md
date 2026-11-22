# AtClient

`Common/Utilities/AT/AtClient.h`: calls the commands of a description, and reads the replies and URCs. The calls queue requests. `Process(nowMs)` writes them, reads the module's lines, runs the handlers and times requests out. Nothing in the client blocks, writes or reads a clock outside `Process`.

---

## Table of Contents

- [Declaration](#declaration)
- [Calls](#calls)
- [Reply Handlers](#reply-handlers)
- [Statuses](#statuses)
- [Feed and Process](#feed-and-process)
- [Timing, Queue and Drain](#timing-queue-and-drain)
- [URCs](#urcs)
- [Echo](#echo)
- [Values on the Line](#values-on-the-line)
- [Limits](#limits)

---

## Declaration

```cpp
template<typename Set, typename Binding, typename... Modules>
class AtClient;

using ClientBinding = AtClientBinding<AtUrcOn<"WIFI GOT IP", &OnGotIp>, AtUrcOn<"+IPD", &OnIpd>>;
AtClient<Sensor, ClientBinding, AtQueue<2>, AtEcho> sensor(out);
```

- `Set`: the description.
- `Binding`: the URC handlers, `AtClientBinding<AtUrcOn<"prefix", &Handler>, ...>`. A URC with no handler is read and ignored.
- `Modules`: `AtQueue<N>` for the queue depth (default 2, the request on the wire included), and `AtEcho` for echo skipping. A module the list does not hold adds no code. A type that is neither is a compile error.
- `out`: a `Print&`, where the client writes its requests.

---

## Calls

Each call queues one request and returns an `AtStatus`. It never writes.

| Call | Line sent | Handler (template argument) |
|---|---|---|
| `Read<"+X", &H>()` | `AT+X?` | `void H(Result<T, AtStatus>)` |
| `Write<"+X", &H>(args...)` | `AT+X=a,b` | `void H(AtStatus)` |
| `Run<"+X", &H>()` | `AT+X` | `void H(AtStatus)` |
| `Test<"+X", &H>()` | `AT+X=?` | `void H(AtStatus)` |
| `Value<"X", &H>(args...)` | `ATX1` (basic) | `void H(AtStatus)` |

- The name is a template argument, and it is checked against the description. A name it does not declare is a compile error.
- A handler whose signature does not match the form is a compile error.
- `Write` and `Value` take exactly the types of their arguments: `bool`, the integer types, `std::string_view` for a string and `std::optional<...>` for an optional one. A value of another type is a compile error, even when it would fit. An `int` for an `int8` argument must be written `int8{5}`. The check is exact on purpose, so a value is never cut without a word.
- A string longer than its capacity returns `invalidParameter`, and nothing is queued. The check comes first, so a full queue does not hide it.
- The client does not check an integer against its argument's `Range()`. A value outside the range is encoded and queued as given, and the module decides whether to accept it.
- A full queue returns `busy`.
- A `Test` reply has no declared info line in this part. Its info line is ignored, and its status is the final line.

```cpp
sensor.Write<"+JOIN", &OnJoin>(std::string_view("net"), std::optional<std::string_view>(std::string_view("pw")));   // AT+JOIN="net","pw"
sensor.Write<"+JOIN", &OnJoin>(std::string_view("net"), std::optional<std::string_view>());                       // AT+JOIN="net"
```

An absent optional argument at the end is not written.

---

## Reply Handlers

```cpp
void OnTemp(Result<int16, AtStatus> result) {
	if (result.IsOk()) {
		// result.Value() is the value of the info line
	} else {
		// result.Error() is the status
	}
}

void OnLed(AtStatus status) {
	// status.status is ResultStatus::ok, error, timeout, ...; status.cmeCode is the CME code
}
```

A handler is called only from `Process()`: when a final line ends its request, when a request times out, or when a drain ends. It is never called from a call that queues.

---

## Statuses

| Module's line | Status |
|---|---|
| `OK` | `ok` |
| `ERROR` | `error` |
| `+CME ERROR: n` | `error`, with `cmeCode` n |
| `+CME ERROR: text` | `error`, with the code of `text` in the dialect's table, or no code |
| a declared final, such as `SEND OK` | `ok` |
| a declared failure, such as `FAIL` | `error` |
| no final line before the timeout | `timeout` |
| an info line that does not parse, and an OK with no info line | `invalidData` |
| a string in the info line that is longer than its capacity | `invalidData` (the value does not parse, so the reply waits for its final line) |
| the queue is full at the call | `busy` (from the call) |
| an argument too long at the call | `invalidParameter` (from the call) |

An info line that does not parse gives `invalidData` at the final line, and the reply waits for that line. A bad info line followed by ERROR is `error`, since the final line decides.

The first info line that parses is the value. A second one is ignored.

---

## Feed and Process

- `bool Feed(uint8 byte)`: takes a byte from the receive interrupt into a 64-byte ring. It returns false, and the byte is counted, when the ring is full.
- `void Process(uint32 nowMs)`: in the main loop, with the time in milliseconds. It sends the queued request, then reads the bytes, then times out the open request and ends its drain. The bytes come first, so a reply already in the ring at the deadline is read.
- `uint32 DroppedBytes()`: the bytes the ring has dropped, counted when it is called.

A request that a final line completes is replaced by the next one in the same place in the byte stream. The result does not depend on how the bytes were split across `Feed` and `Process` calls.

---

## Timing, Queue and Drain

- The timeout of a command is its `.TimeoutMs()`, or the set's default. A timeout is checked as `(now - start) >= timeout`, so it is correct across the 32-bit wrap.
- A request that reaches its timeout is drained. The client sends `AT`. The first final line after the timeout (the late reply's final, or the drain's own OK when the module never answers the request) only moves the drain on. The next `OK` ends the drain, and so does the drain's own timeout (the set's default). The request then ends with `timeout`, and the next request is sent.
- So a late `OK` cannot end the drain early, and a late final line that comes before the drain ends cannot answer the next request. The cost: a module that answers `AT` and nothing else ends the drain at its own timeout, not at its `OK`.
- Limit: a late `OK` that arrives after the drain has ended is taken by the next request. An `OK` cannot be told from the next request's own `OK`, so the protocol cannot rule this out.
- Nothing else is sent during a drain.
- A URC is still handled during a drain. Only the first final line and then the next `OK` are read as the drain's; the other lines are dropped.
- The queue holds the request on the wire and the ones behind it. The handler of a finished request runs after it has left the queue, so a handler may queue another request.

---

## URCs

`Binding` holds one handler for each URC prefix that the client reads:

```cpp
void OnIpd(uint16 length, std::string_view tag, std::optional<bool> flag) {
	// +IPD,4,"ab",1 gives length 4, tag "ab" and flag true
}
```

- A line whose prefix is a declared URC is handled at once, also between the info lines of a reply. It never ends the reply.
- The prefix ends at `:`, `,` or the end of the line. The arguments follow the first comma, and are separated by commas, outside quotes.
- A URC whose arguments do not parse, that has too many arguments, that lacks a required one, or whose integer field is outside its argument's range, is dropped and its handler is not called. The range check is the same as the check for a value that overflows its type. Trailing optional arguments may be absent, and an empty optional argument is absent.
- A URC with no arguments is handled on its prefix alone.

---

## Echo

With `AtEcho` in the modules, the client drops the first line after a request when it is the request's text. This is the echo that ATE1 sends. Without `AtEcho` the echoed line is an unknown line, and it is ignored. The result is the same either way, unless the echo happens to be a URC prefix.

The client does not send `ATE0` by itself. An application that needs echo off sends `Value<"E", &OnDone>(uint8{0})`, which needs `.Echo()` in the description: without it `E` is not a name of the set, and the call is a compile error. The same holds for `V` and `Q` with `.Verbose()` and `.Quiet()`.

---

## Values on the Line

Each value is read with the grammar of the description: the integers in decimal, a bool as `0` or `1`, and a string in quotes with `\"` and `\\` as the escapes. A value that does not parse is `invalidData`. After the `:` of an info line one blank is skipped, and any other blank makes the value fail.

An integer that does not fit its type, or that is outside the declared range of its URC argument, does not parse. A `Read` reply is checked against its type only, since `Read` declares no argument, so a reply above the type's maximum is `invalidData`.

---

## Limits

- The client takes no numeric result codes (`0`, `4`, from ATV0). A module that is set to numeric codes times out.
- A `+CME ERROR:` line whose text is not in the set's table gives `error` with no code. The code is lost, since only the text arrives. Declare the text with `.CmeText<code, "text">()` to keep the code.
- A line longer than the client's line buffer is never a URC or a final line. An info line of that length gives `invalidData`.
- The line buffer and the queue are sized from the description, so a line of any valid value fits.
- The ring holds 64 bytes. A receive interrupt that runs faster than `Process` loses bytes, and `DroppedBytes()` counts them.
