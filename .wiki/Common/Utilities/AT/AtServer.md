# AtServer

`Common/Utilities/AT/AtServer.h`: answers the AT commands of a description. It reads the bytes of the host's lines, runs the handler of each command, and writes the result lines and the queued URCs.

---

## Table of Contents

- [Declaration](#declaration)
- [Handlers](#handlers)
- [Feed and Process](#feed-and-process)
- [Lines and Results](#lines-and-results)
- [Failure Codes](#failure-codes)
- [Info Lines](#info-lines)
- [URCs](#urcs)
- [Limits](#limits)
- [Dropped Bytes](#dropped-bytes)

---

## Declaration

```cpp
template<typename Set, typename App, typename Binding, std::size_t UrcDepth = 4, std::size_t RingSize = 64>
class AtServer;

AtServer<Sensor, App, ServerBinding> server(app, out);
```

- `Set`: the description (`AtCommandSet<...>`).
- `App`: the application. Each handler gets an `AtCall<App>` first, and reads and changes `call.app`.
- `Binding`: the handlers, `AtServerBinding<AtOn<"+X", &Handler>, ...>`.
- `UrcDepth`: the URC queue depth.
- `RingSize`: the input ring, a power of two.
- `out`: a `Print&`, where the server writes.

---

## Handlers

A handler is a static function or a captureless lambda (`+[]`). Its signature says which form it serves, and the compile-time check refuses a mismatch with its message.

| Form | Signature |
|---|---|
| `Read<T>`, `Run<T>` | `Result<T, AtStatus> Name(AtCall<App> call)` |
| `Read(row)`, `Run(row)` | `Result<S, AtStatus> Name(AtCall<App> call)`, where S is the row's type |
| `ReadRows(row)`, `Rows(row)` | `AtStatus Name(AtCall<App> call, AtRowWriter<S>& out)` |
| `Write(args...)` | `AtStatus Name(AtCall<App> call, args...)` |
| `Run()` | `AtStatus Name(AtCall<App> call)` |
| `Value` (basic) | `AtStatus Name(AtCall<App> call, arg)` |
| `Test()` | none: the text comes from the argument ranges |

A string argument arrives as `std::string_view`, and an optional one as `std::optional<...>`. An integer arrives only inside its range. A name can have one handler for each of its forms, and the signature chooses. A command form with no handler is a compile error, except the write form of a `.Prompt()` command: that one has no handler, and its line is answered with `ERROR`. A handler bound to a prompt command is a compile error (see [AtDataMode](AtDataMode.md#the-server)).

A handler that returns an error status fails the line:

```cpp
AtStatus Deny(AtCall<App> call) {
	static_cast<void>(call);
	return AtCmeError(3);       // +CME ERROR: 3, or its text when the dialect asks for text
}

AtStatus Bad(AtCall<App> call) {
	static_cast<void>(call);
	return AtStatus{ResultStatus::error};   // ERROR
}
```

A reply form (`Read<T>`, `Run<T>`, `Read(row)`, `Run(row)`) writes its info line when its handler returns a value. A handler that returns an error writes no info line, and the line ends with the error. The rows forms write their lines as the handler adds them, through [AtRowWriter](AtRows.md#the-server-side).

---

## Feed and Process

- `bool Feed(uint8 byte)`: takes a byte from the receive interrupt into the ring. It returns false, and the byte is counted as dropped, when the ring is full. It does no parsing.
- `void Process()`: in the main loop. It reads the ring, parses the lines, runs the handlers at each `;` and at the end of each line, writes the results, and writes the queued URCs.

Output is written in `Process()` only, never in `Feed()`.

```cpp
void OnReceiveInterrupt() {
	server.Feed(static_cast<uint8>(UartReadByte()));      // from the interrupt: copies one byte, and counts it when the ring is full
}

void MainLoop() {
	server.Process();                                     // from the main loop: parses, runs the handlers, writes the replies
}
```

---

## Lines and Results

- A line ends at CR or LF, and CRLF or LFCR is one end. An empty line produces nothing.
- With `.Registers()` in the dialect, a line ends at S3 (see [AtDialect](AtDialect.md#input-line-end)). While S3 is 13, LF also ends a line, and CRLF or LFCR is one end. While S3 is any other value, only S3 ends a line, and a CR or LF that is not S3 is dropped.
- The forms are `AT+X=?`, `AT+X?`, `AT+X=a,b`, `AT+X`, and `AT` alone (answered with OK). Names and `AT` are case-insensitive. A line without `AT` gives ERROR.
- `;` runs the commands of a line in turn. The line gives one final result code, and the first error stops the rest of the line.
- A result is `OK`, `ERROR`, or `+CME ERROR: n` (or its text). With ATV0 the codes are `0` and `4`, and with ATQ1 no result is written.
- An info line and a verbose result are written as `S3 S4 text S3 S4`, with S3 and S4 (CR and LF by default). A numeric result (ATV0) is its code and S3 only, such as `0` then S3.
- A declared command is answered with its handler's status. A test form writes the range of each write argument: the declared range of an integer, which is the range of its type unless `.Range()` sets one, such as `(-128-127)`; `(0,1)` for a bool; `(0-8)` for a string.

Argument fields are read with the value grammar of [AtClient](AtClient.md). A field that does not parse, a missing required field, or a form the command does not declare gives ERROR. An integer field outside its argument's range gives ERROR too, the same as a field that overflows its type.

---

## Failure Codes

A handler that returns an error with `failureCode` set, for a command declared with `.FailureCode("+CWJAP")`, writes its code line before the result. The line is the prefix, a colon and the code. With `failureCode` 2 the server writes:

```
+CWJAP:2

ERROR
```

The code line ends with CRLF, and the result starts with its own CRLF. So the code line is followed by a blank line. This is the framing every info line gets from the server (see Info Lines), not something the ESP-AT reference shows: the reference gives `+CWJAP:<error code>` and then `ERROR` with no blank. The blank is checked against the real module on the bench, not assumed.

- Only an error writes the code line, and only when the code is not zero. A handler that returns `OK` writes no code line, even when it sets `failureCode`.
- A command with no `.FailureCode()` never writes a code line.
- Quiet mode (`ATQ1`) stops the result, and not the code line, since the code line is an info line.
- A code line is written before the result of its request. A `+CME ERROR` result that comes with a code is written after the code line too.

---

## Info Lines

An info line is the reply prefix, its separator, and the value:

- With no reply prefix, the prefix is the command's name and a colon, and one blank follows: `+TEMP: 23`.
- With `.Reply(prefix)`, the prefix is that text. A prefix that ends in a colon is followed by one blank (`AT version: 2.2.0.0`). Any other prefix is followed by a comma (`+CIFSR:STAIP,"192.168.1.9"`).
- A value is written with the value grammar. A `RawString` is written as it is, up to the first CR or LF.
- A record or a row is written with its Open and Close texts, as `(4,"net",-72)`.

```cpp
.Command<"+GMR">().Run<RawString<64>>("version").Reply("AT version:")
.Command<"+CIFSR">().Run<String<15>>("ip").Reply("+CIFSR:STAIP")
```

With these, `AT+GMR` writes `AT version: 2.2.0.0(Jul 1 2022)` and `AT+CIFSR` writes `+CIFSR:STAIP,"192.168.1.9"`, each followed by the result line `OK`. Only a set that has a reply prefix of the second kind has code for the comma.

---

## URCs

`server.Urc<"WIFI GOT IP">()` queues a URC with its arguments, and returns `filled` when the queue is full:

```cpp
// The description: .Urc<"+IPD">(Argument<uint16>("length"), Argument<String<8>>("tag"), Optional<bool>("flag"))
String<8> tagText{};                                      // holds ab
server.Urc<"+IPD">(uint16{4}, tagText, std::optional<bool>(true));   // writes +IPD,4,"ab",1
server.Urc<"WIFI GOT IP">();                              // a URC with no arguments
```

- The queue runs under `System::CriticalSection`, so it can be called from a task.
- `Process()` writes a queued URC when no reply is open and no line is partly read. A URC queued while a line is read is written after that line's result.
- A URC is written as its prefix, then `,` and each argument: `+IPD,4,"ab",1`.
- Each value must have exactly the type its argument stores: `uint16` for a `uint16` argument, `String<N>` for a string argument. An `int` for a `uint16` argument, or a `string_view`, is a compile error. The check is exact, so a literal needs a cast such as `uint16{4}`.
- A URC with `Data()` cannot be queued in version 1: `server.Urc<>()` of it is a compile error, since the server writes no raw data (see [AtDataMode](AtDataMode.md#the-server)).

---

## Limits

- A prompt command (`.Prompt()`, such as `AT+CIPSEND`) has no handler in version 1. Its line is answered with `ERROR`, and no data is read (see [AtDataMode](AtDataMode.md#the-server)).
- A URC with `Data()` cannot be queued: `server.Urc<"+IPD">(...)` with a `Data()` argument is a compile error.
- With `ATV0` the server writes numeric result codes (`0` and `4`). The client of this library does not read them, so a client of the same description times out (see [AtClient](AtClient.md#limits)).
- A URC queued while a reply is open is written after that reply's result line. A full queue returns `filled`, and the URC is not written.
- The server has no clock, so it has no timeouts. A command that never returns is the application's problem, since the handlers run in `Process()`.

## Dropped Bytes

`DroppedBytes()` returns the bytes the ring has dropped since the server started. It is counted when it is called, so the number is current when read.

```cpp
uint32 lost = server.DroppedBytes();    // nonzero when the host sent faster than Process() ran
```
