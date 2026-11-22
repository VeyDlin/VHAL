# AtClient

`Common/Utilities/AT/AtClient.h`: calls the commands of a description, and reads the replies and URCs. The calls queue requests. `Process(nowMs)` writes them, reads the module's lines, runs the handlers and times requests out. Nothing in the client blocks, writes or reads a clock outside `Process`.

---

## Table of Contents

- [Declaration](#declaration)
- [Calls](#calls)
- [Ping](#ping)
- [Reply Handlers](#reply-handlers)
- [Rows](#rows)
- [Statuses](#statuses)
- [Failure Codes](#failure-codes)
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
AtClient<Sensor, ClientBinding, AtQueue<2>, AtEcho, AtRows<8>> sensor(out);
```

- `Set`: the description.
- `Binding`: the URC handlers, `AtClientBinding<AtUrcOn<"prefix", &Handler>, ...>`. A URC with no handler is read and ignored.
- `Modules`:
  - `AtQueue<N>` for the queue depth (default 2, the request on the wire included);
  - `AtEcho` for echo skipping;
  - `AtRows<N>` for the rows of a `Rows` or `ReadRows` call, with N rows in one list (see [AtRows](AtRows.md)).
  A module the list does not hold adds no code. A type that is none of these is a compile error.
- `out`: a `Print&`, where the client writes its requests.

---

## Calls

Each call queues one request and returns an `AtStatus`. It never writes.

| Call | Line sent | Handler (template argument) |
|---|---|---|
| `Read<"+X", &H>()` | `AT+X?` | `void H(Result<T, AtStatus>)`, with T the value or the record |
| `Write<"+X", &H>(args...)` | `AT+X=a,b` | `void H(AtStatus)` |
| `Run<"+X", &H>()` | `AT+X` | `void H(AtStatus)` for a run form with no reply |
| `Run<"+X", &H>()` | `AT+X` | `void H(Result<T, AtStatus>)` for a run form with a value or a record |
| `Test<"+X", &H>()` | `AT+X=?` | `void H(AtStatus)` |
| `Ping<&H>()` | `AT` | `void H(AtStatus)`, see [Ping](#ping) |
| `Value<"X", &H>(args...)` | `ATX1` (basic) | `void H(AtStatus)` |
| `SendData<"+X", &H>(args..., data)` | `AT+X=a,b`, then the data after the prompt | `void H(AtStatus)` (needs `AtDataMode`, see [AtDataMode](AtDataMode.md)) |
| `ReadRows<"+X", &H>()` | `AT+X?` | `void H(const Result<AtRowList<S, N>, AtStatus>&)` |
| `Rows<"+X", &H>()` | `AT+X` | `void H(const Result<AtRowList<S, N>, AtStatus>&)` |
| `ReadRowsVisit<"+X", &OnRow, &OnDone>()` | `AT+X?` | `void OnRow(const S&)`, then `void OnDone(AtStatus)` |
| `RowsVisit<"+X", &OnRow, &OnDone>()` | `AT+X` | `void OnRow(const S&)`, then `void OnDone(AtStatus)` |

- The name is a template argument, and it is checked against the description. A name it does not declare is a compile error.
- A handler whose signature does not match the form is a compile error. `Read` on a rows form, `Rows` on a value form, and the other mismatches each name the call to use.
- `Write` and `Value` take exactly the types of their arguments: `bool`, the integer types, `std::string_view` for a string and `std::optional<...>` for an optional one. A value of another type is a compile error, even when it would fit. An `int` for an `int8` argument must be written `int8{5}`. The check is exact on purpose, so a value is never cut without a word.
- A string longer than its capacity returns `invalidParameter`, and nothing is queued. The check comes first, so a full queue does not hide it.
- The client does not check an integer against its argument's `Range()`. A value outside the range is encoded and queued as given, and the module decides whether to accept it.
- A full queue returns `busy`.
- A `Test` reply has no declared info line. Its info line is ignored, and its status is the final line.

```cpp
sensor.Write<"+JOIN", &OnJoin>(std::string_view("net"), std::optional<std::string_view>(std::string_view("pw")));   // AT+JOIN="net","pw"
sensor.Write<"+JOIN", &OnJoin>(std::string_view("net"), std::optional<std::string_view>());                       // AT+JOIN="net"
```

An absent optional argument at the end is not written.

Each call once, with the line it sends. The handlers are the ones of the reply handler and rows sections below:

```cpp
sensor.Read<"+TEMP", &OnTemp>();                                       // AT+TEMP?
sensor.Write<"+LED", &OnLed>(true);                                    // AT+LED=1
sensor.Run<"+CWQAP", &OnStatus>();                                     // AT+CWQAP, a run form with no reply
sensor.Run<"+GMR", &OnGmr>();                                          // AT+GMR, a run form with a value
sensor.Test<"+CWMODE", &OnStatus>();                                   // AT+CWMODE=?
sensor.Value<"E", &OnStatus>(uint8{0});                                // ATE0, the builtin echo
sensor.SendData<"+CIPSEND", &OnStatus>(uint16{5}, std::span<const uint8>(kPayload));   // AT+CIPSEND=5, then the data
sensor.Rows<"+CWLAP", &OnList>();                                      // AT+CWLAP, the rows of a run form
sensor.ReadRows<"+SCAN", &OnList>();                                   // AT+SCAN?, the rows of a read form
sensor.RowsVisit<"+CWLAP", &OnRow, &OnRowsDone>();                     // AT+CWLAP, OnRow for each row as it arrives
sensor.ReadRowsVisit<"+SCAN", &OnRow, &OnRowsDone>();                  // AT+SCAN?, the same for a read form
```

With `AtAwait`, each call has an `Await` twin that waits for the reply (see [AtAwait](AtAwait.md#the-calls)).

### SendData

`SendData` sends the request of a prompt command (see [AtDataMode](AtDataMode.md#prompt-commands)). Its values are those of the write form, checked as `Write` checks them, and its last value is the data, a `std::span<const uint8>`:

```cpp
static constexpr uint8 kPayload[] = {'h', 'e', 'l', 'l', 'o'};
sensor.SendData<"+CIPSEND", &OnSent>(uint16{5}, std::span<const uint8>(kPayload));   // AT+CIPSEND=5, then the five bytes after the '>' prompt
```

- The request line is written first. Before the prompt, `OK` does not end it, and `ERROR`, `+CME ERROR` and the declared failures do. The data is written when the `>` comes, and the declared finals and failures end the request after it.
- The data is not copied. The span must stay valid until the handler runs.
- `Write` on a prompt command, and `SendData` on a command without `.Prompt()`, are compile errors. `SendData` in a set without `AtDataMode` is a compile error too.

---

## Ping

`Ping<&H>()` checks that the module answers. It writes the bare `AT`, and its handler gets the status of the final line:

```cpp
void OnPing(AtStatus status) {
	// ok when the module answers OK, error for ERROR, timeout when it does not answer
}

sensor.Ping<&OnPing>();     // AT
```

- The request uses the set's default timeout. A timeout starts the drain, as for any request (see [Timing, Queue and Drain](#timing-queue-and-drain)).
- It queues like every other call. It returns `busy` when the queue is full, and it waits behind the requests before it.
- With `AtEcho`, the echo of the `AT` line is dropped.
- With `AtAwait`, `AwaitPing()` waits for the status and returns it (see [AtAwait](AtAwait.md#the-calls)).
- A client that never calls `Ping` has no code for it. The drain writes its `AT` through the same code as the request.

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
	// status.status is ResultStatus::ok, error, timeout, ...; status.cmeCode is the CME code;
	// status.failureCode is the code of a failure line, or zero (see Failure Codes)
}
```

- A `Read<T>` or a `Run<T>` handler gets the value of its info line, a `Read(row)` or `Run(row)` handler gets the record, and a `Run<RawString<N>>` handler gets the raw text. A string value is `String<N>` or `RawString<N>`, as the type of the form.
- A handler is called only from `Process()`: when a final line ends its request, when a request times out, or when a drain ends. It is never called from a call that queues.
- The value or record of a reply is copied before the handler runs, so the handler may keep it.

---

## Rows

`ReadRows` and `Rows` hand the rows of a list to one handler at the final line. They need `AtRows<N>` in the modules:

```cpp
void OnList(const Result<AtRowList<Network, 8>, AtStatus>& result) {
	if (result.IsOk()) {
		for (const Network& network : result.Value()) {
			// one row
		}
	}
}
```

`RowsVisit` and `ReadRowsVisit` call `OnRow` for each row as it arrives, and `OnDone` at the end. They need no `AtRows`. The row types, the list and the server side are in [AtRows](AtRows.md).

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
| a code line before an error final, with `.FailureCode()` (see [Failure Codes](#failure-codes)) | `error`, with `failureCode` set to the code |
| no final line before the timeout | `timeout` |
| an info line of a value or a record that does not parse, and an OK with no info line | `invalidData` |
| a row that does not parse, for `ReadRows`, `Rows` and the visits | `invalidData` |
| a row past the capacity of `AtRows<N>` | `ok`: the row is counted in `Skipped()` |
| a string in the info line that is longer than its capacity | `invalidData` (the value does not parse, so the reply waits for its final line) |
| the queue is full at the call | `busy` (from the call) |
| an argument too long at the call | `invalidParameter` (from the call) |

An info line that does not parse gives `invalidData` at the final line, and the reply waits for that line. A bad info line followed by ERROR is `error`, since the final line decides.

A value or a record has one info line. A line that does not parse, or a second info line, makes the reply `invalidData`, so the handler never gets a value that could be the wrong one. A reply with several lines is read with `Rows`. A row is different: every row is read, and a row that does not parse makes the whole reply `invalidData`.

---

## Failure Codes

Some commands send a code line before they fail. ESP-AT sends `+CWJAP:2` and then `ERROR` when a join fails with code 2. The command declares the prefix of its code line:

```cpp
.Command<"+CWJAP">().Read(...).Write(...).FailureCode("+CWJAP")
```

- A code line is the prefix, a colon, an optional blank, and an unsigned integer that fits 8 bits. `+CWJAP:2` and `+CWJAP: 2` both give the code 2. The largest code is 255.
- The client reads a code line only during a form that has no reply: a write, a test, or a run with no value. A form with a reply reads a line with the prefix as its reply. So `AT+CWJAP?` gives its record, and with `Read<uint8>` the line `+CWJAP:2` is the value 2.
- A line with the prefix and no integer, or with a number above 255, is an unknown line. The client ignores it.
- The code is kept in the status only when the request ends with an error: `ERROR`, `+CME ERROR` or a declared failure. An `OK` ends the request with `failureCode` 0, and so does a timeout. A second code line replaces the first one.
- A URC between the code line and the final line is handled at once. The code stays until the final line.
- The code is `AtStatus::failureCode`, next to `cmeCode`. A status with no code has `failureCode` 0.

```cpp
void OnJoin(AtStatus status) {
	if (status.failureCode == 2) {
		// the password is wrong
	}
}

sensor.Write<"+CWJAP", &OnJoin>(std::string_view("net"), std::string_view("bad"), std::optional<std::string_view>());
```

The compile checks of `FailureCode()`: the prefix is not empty, it has no colon, comma or blank (the colon after it is added), the command has one `FailureCode()`, and the prefix is neither a URC prefix nor a final line of the set. A set with no `FailureCode()` has no code for the line and no table of prefixes.

`AtStatus` is 4 bytes: the status, the failure code (one byte) and the CME code. Build one with named fields, such as `AtStatus{.status = ResultStatus::error, .failureCode = 2}`, or with `AtCmeError(code)` for a CME error.

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
- With `AtAwait` in the client's modules, a request of an `Await` call is not handed to a handler. Its reply waits in its entry until the calling task copies it out. The wait, the safety limit and the rule that `Await` never runs in `Process()` are in [AtAwait](AtAwait.md).

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

### A URC with Data()

A URC whose last argument is `Data()` takes raw bytes after its length. Its handler gets the arguments before `Data()`, then the offset of the chunk, the chunk, and whether it is the last one:

```cpp
void OnIpd(uint16 length, uint16 offset, std::span<const uint8> chunk, bool last) {
	// chunks of at most 32 bytes, in order; last is set once, on the final chunk
}
```

The run's bytes never form lines, and a request that is open is not ended by them. The details, and the rules of the length, are in [AtDataMode](AtDataMode.md#data-urcs).

---

## Echo

With `AtEcho` in the modules, the client drops the first line after a request when it is the request's text. This is the echo that ATE1 sends. Without `AtEcho` the echoed line is an unknown line, and it is ignored. The result is the same either way, unless the echo happens to be a URC prefix.

The client does not send `ATE0` by itself. An application that needs echo off sends `Value<"E", &OnDone>(uint8{0})`, which needs `.Echo()` in the description: without it `E` is not a name of the set, and the call is a compile error. The same holds for `V` and `Q` with `.Verbose()` and `.Quiet()`.

---

## Values on the Line

Each value is read with the grammar of the description: the integers in decimal, a bool as `0` or `1`, and a string in quotes with `\"` and `\\` as the escapes. A raw string is the text as it is, to the end of the line.

After an info line's reply prefix the client skips:

- one comma, when the reply prefix does not end in a colon (`+CIFSR:STAIP,"192.168.1.9"`), and then
- one blank.

So `+TEMP: 23`, `AT version: 2.2.0.0`, and `+CIFSR:STAIP,"192.168.1.9"` all give the value that follows. A value that does not parse is `invalidData`.

A reply prefix that ends in a letter or a digit ends a word: `+CIFSR:STAIP` takes `+CIFSR:STAIP,"192.168.1.9"` but not `+CIFSR:STAIP6LL,"fe80::1"`.

An integer that does not fit its type, or that is outside the declared range of its URC argument or row field, does not parse. A `Read` reply is checked against its type only, since `Read` declares no argument, so a reply above the type's maximum is `invalidData`.

---

## Limits

- The client takes no numeric result codes (`0`, `4`, from ATV0). A module that is set to numeric codes times out.
- A `+CME ERROR:` line whose text is not in the set's table gives `error` with no code. The code is lost, since only the text arrives. Declare the text with `.CmeText<code, "text">()` to keep the code.
- A line longer than the client's line buffer is never a URC or a final line. An info line of a value that is that long gives `invalidData`. A record or a row longer than the buffer is read from the characters the buffer keeps, which hold every declared field, and its extra fields are skipped.
- The line buffer and the queue are sized from the description, so a line of any valid value fits.
- The ring holds 64 bytes. A receive interrupt that runs faster than `Process` loses bytes, and `DroppedBytes()` counts them.
- The late reply after a drain. A reply that comes after the drain has ended can answer the next request, when it is an `OK`. The drain's `AT` cannot tell the late `OK` from its own, so the protocol cannot rule this out. Other late finals, such as `ERROR`, cannot answer the next request. See [Timing, Queue and Drain](#timing-queue-and-drain).
- Numeric result codes. With `ATV0` the module sends `0` and `4`, and the client does not read them. A request answered with numbers times out, and its drain ends at its own timeout, since the answer to the drain's `AT` is numeric too.
- Verbose text that the set does not know. A `+CME ERROR: <text>` line with a text that is not in the set's `.CmeText<>()` table gives `error` with code 0 (see [AtDialect](AtDialect.md#limits-and-open-points)).
