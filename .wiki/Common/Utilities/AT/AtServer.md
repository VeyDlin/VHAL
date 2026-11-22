# AtServer

`Common/Utilities/AT/AtServer.h`: answers the AT commands of a description. It reads the bytes of the host's lines, runs the handler of each command, and writes the result lines and the queued URCs.

---

## Table of Contents

- [Declaration](#declaration)
- [Handlers](#handlers)
- [Feed and Process](#feed-and-process)
- [Lines and Results](#lines-and-results)
- [URCs](#urcs)
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
| `Read<T>` | `Result<T, AtStatus> Name(AtCall<App> call)` |
| `Write(args...)` | `AtStatus Name(AtCall<App> call, args...)` |
| `Run()` | `AtStatus Name(AtCall<App> call)` |
| `Value` (basic) | `AtStatus Name(AtCall<App> call, arg)` |
| `Test()` | none: the text comes from the argument ranges |

A string argument arrives as `std::string_view`, and an optional one as `std::optional<...>`. An integer arrives only inside its range. A name can have one handler for each of its forms, and the signature chooses. A command form with no handler is a compile error.

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

---

## Feed and Process

- `bool Feed(uint8 byte)`: takes a byte from the receive interrupt into the ring. It returns false, and the byte is counted as dropped, when the ring is full. It does no parsing.
- `void Process()`: in the main loop. It reads the ring, parses the lines, runs the handlers at each `;` and at the end of each line, writes the results, and writes the queued URCs.

Output is written in `Process()` only, never in `Feed()`.

---

## Lines and Results

- A line ends at CR or LF, and CRLF or LFCR is one end. An empty line produces nothing.
- With `.Registers()` in the dialect, a line ends at S3 (see [AtDialect](AtDialect.md#input-line-end)). While S3 is 13, LF also ends a line, and CRLF or LFCR is one end. While S3 is any other value, only S3 ends a line, and a CR or LF that is not S3 is dropped.
- The forms are `AT+X=?`, `AT+X?`, `AT+X=a,b`, `AT+X`, and `AT` alone (answered with OK). Names and `AT` are case-insensitive. A line without `AT` gives ERROR.
- `;` runs the commands of a line in turn. The line gives one final result code, and the first error stops the rest of the line.
- An info line is the reply prefix (the name and a colon, or the command's `.Reply()`), one blank, and the value: `+TEMP: 23`.
- A result is `OK`, `ERROR`, or `+CME ERROR: n` (or its text). With ATV0 the codes are `0` and `4`, and with ATQ1 no result is written.
- An info line and a verbose result are written as `S3 S4 text S3 S4`, with S3 and S4 (CR and LF by default). A numeric result (ATV0) is its code and S3 only, such as `0` then S3.
- A declared command is answered with its handler's status. A test form writes the range of each write argument: the declared range of an integer, which is the range of its type unless `.Range()` sets one, such as `(-128-127)`; `(0,1)` for a bool; `(0-8)` for a string.

Argument fields are read with the value grammar of [AtClient](AtClient.md). A field that does not parse, a missing required field, or a form the command does not declare gives ERROR. An integer field outside its argument's range gives ERROR too, the same as a field that overflows its type.

---

## URCs

`server.Urc<"WIFI GOT IP">()` queues a URC with its arguments, and returns `filled` when the queue is full:

```cpp
server.Urc<"+IPD">(uint16{4}, tag, true);   // tag is a String<8> holding ab: +IPD,4,"ab",1
```

- The queue runs under `System::CriticalSection`, so it can be called from a task.
- `Process()` writes a queued URC when no reply is open and no line is partly read. A URC queued while a line is read is written after that line's result.
- A URC is written as its prefix, then `,` and each argument: `+IPD,4,"ab",1`.
- Each value must have exactly the type its argument stores: `uint16` for a `uint16` argument, `String<N>` for a string argument. An `int` for a `uint16` argument, or a `string_view`, is a compile error. The check is exact, so a literal needs a cast such as `uint16{4}`.

---

## Dropped Bytes

`DroppedBytes()` returns the bytes the ring has dropped since the server started. It is counted when it is called, so the number is current when read.
