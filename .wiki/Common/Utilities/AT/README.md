# AT

`Common/Utilities/AT/`: AT commands for embedded systems (VHAL), one description for both sides. A board answers AT commands for a host, and drives a module (Wi-Fi, a modem, a BLE dongle) with them. The description lists the commands, their forms, their argument types and their URCs. A server answers from it, and a client calls from it. No heap, no exceptions, no RTTI, no `std::function`, and no virtual calls in the AT code.

---

## Table of Contents

- [Quick Start](#quick-start)
- [The Description](#the-description)
- [Rules Checked at Compile Time](#rules-checked-at-compile-time)
- [The Server](#the-server)
- [The Client](#the-client)
- [Headers](#headers)
- [Costs](#costs)
- [What Stage 2 Adds](#what-stage-2-adds)
- [Tests](#tests)

---

## Quick Start

```cpp
#include <Utilities/AT/AtClient.h>
#include <Utilities/AT/AtServer.h>

using Sensor = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Defaults(AtDefaults().TimeoutMs(2000))
		.Dialect(AtDialect().Echo().Verbose().Quiet())
		.Command<"+TEMP">().Read<int16>("celsius")                                // AT+TEMP?   -> +TEMP: 23
		.Command<"+LED">().Write(Argument<bool>("on"))                            // AT+LED=1
		.Command<"+JOIN">().Write(Argument<String<32>>("ssid"), Optional<String<64>>("password")).TimeoutMs(15000)
		.Basic<"X">().Value(Argument<uint8>("on"))                                 // ATX1
		.Urc<"WIFI GOT IP">();
}>;

// Server, on the board that answers
using ServerBinding = AtServerBinding<AtOn<"+TEMP", &Temp>, AtOn<"+LED", &Led>>;
AtServer<Sensor, App, ServerBinding> server(app, out);
server.Feed(byte);                  // from the receive interrupt
server.Process();                   // main loop

// Client, on the board that asks
using ClientBinding = AtClientBinding<AtUrcOn<"WIFI GOT IP", &OnGotIp>>;
AtClient<Sensor, ClientBinding, AtQueue<2>, AtEcho> sensor(out);
sensor.Feed(byte);                  // from the receive interrupt
sensor.Read<"+TEMP", &OnTemp>();    // queues the request; the reply comes in Process()
sensor.Process(nowMs);              // main loop
```

`App`, `Temp`, `Led`, `OnTemp` and `OnGotIp` are the application's own types and functions. The page for each side shows their signatures.

---

## The Description

`AtCommandSet<+[] { return AtCommandSetBuilder()...; }>` is built at compile time from a chain of calls. Each call returns a builder type that holds one more item, so the type of the chain carries every form and every argument.

| Call | Declares |
|---|---|
| `.Defaults(AtDefaults().TimeoutMs(n))` | the timeout of every command that does not give its own |
| `.Modules(tag)` | a set-level module. `AtDataMode` is accepted now and adds no behaviour until stage 2 |
| `.Dialect(AtDialect()...)` | the server's dialect options, see [AtDialect](AtDialect.md) |
| `.Command<"+X">()` | a command, with the forms and options that follow it |
| `.Basic<"X">()` | a basic command: one letter, whose value follows it with no `=`, as in `ATE0` |
| `.Urc<"prefix">(args...)` | a URC and its arguments |

Forms of a command, in the order a chain gives them:

| Form | Line | Declared by |
|---|---|---|
| test | `AT+X=?` | `.Test()` |
| read | `AT+X?` | `.Read<T>("name")` |
| write | `AT+X=a,b` | `.Write(args...)` |
| run | `AT+X` | `.Run()` |
| value (basic) | `ATX1` | `.Value(args...)` on a `.Basic<"X">()` |

Options of the open command: `.Reply("+CIFSR:STAIP")` (the info line's prefix, instead of the name and a colon), `.Finals("SEND OK")` (a final line that is a success), `.Failures("FAIL")` (a final line that is an error), `.TimeoutMs(n)`.

A required argument is `Argument<T>("name")`, the class the command line uses (`CommandArgument.h`), so a console and an AT set in one program name their arguments the same way. An optional argument is `Optional<T>("name")`, for a field that may be empty. Optional arguments come last.

| Type | On the line |
|---|---|
| `int8`, `int16`, `int32`, `int64` | signed decimal |
| `uint8`, `uint16`, `uint32` | unsigned decimal |
| `bool` | `0` or `1` only |
| `String<N>` | quoted text of at most N characters; `\"` and `\\` are the escapes |

The server gets a `std::string_view` for a string argument and a `std::optional` for an optional one. The client takes the same types when it calls.

### Ranges and Defaults

- An integer argument takes its whole type. `.Range(low, high)` sets the values it accepts: `Argument<uint8>("level").Range(2, 9)`. A second `.Range()` replaces the first. `Optional<T>` takes `.Range()` the same way.
- The server refuses a value outside the range the way it refuses one that overflows its type: the reply is `ERROR`.
- The client drops a URC field outside the range, as it drops one that overflows. A `Read` reply is checked against its type only, since `Read` declares no argument.
- The client does not check the values it sends. A value outside the range is written as given, and the module decides.
- `.Default()` is refused in an AT description. An argument that may be absent is `Optional<T>`.
- An argument of an enum type, a float or another custom type is refused. An AT argument is an integer, `bool` or `String<N>`.

---

## Rules Checked at Compile Time

Each rule is a compile error with its message, as in the other VHAL builders. The snippets that check each one are in `Common/Utilities/AT/.tool/sim/BuilderErrors/`. The list covers the names (a command starts with `+` and has no space, a basic command is one letter, a name is declared once), the URC prefixes (no colon or comma, not a prefix of a command name or a reply prefix, declared once), the forms (once each per command, the value form only on a basic command), the arguments (an argument is an integer, `bool` or `String<N>` with no default, a required argument does not follow an optional one, no two names are the same, a string has a capacity above zero, a range has its low at most its high and both bounds fit the type), the options (a timeout above zero, `Defaults()` and `Dialect()` given once, a module is a module tag), and the handlers (the signature of each server and client handler, and the name each one is bound to).

A name the description does not declare is an error at the call: `client.Read<"+TMEP", &OnTemp>()` fails with "AT command name is not in the description".

---

## The Server

[AtServer](AtServer.md) runs the description on the board that answers. It reads the lines the host sends, runs the handlers, and writes the result lines and the queued URCs.

## The Client

[AtClient](AtClient.md) calls the description from the board that asks. The calls only queue a request. `Process(nowMs)` writes the request, reads the lines, runs the reply handlers and times requests out. Nothing in the client blocks or reads a clock.

---

## Headers

| Header | What it holds | Page |
|---|---|---|
| `AtCommandSet.h` | `AtCommandSet`, `AtCommandSetBuilder`, the rules of the description | this page |
| `AtForms.h`, `AtArgument.h`, `AtOptions.h` | forms, options, `Optional`, the `AtArgument` each declared argument becomes, `AtDefaults` | this page |
| `AtValue.h` | the value grammar: readers and writers of every type | this page |
| `AtBinding.h` | `AtServerBinding`, `AtClientBinding`, `AtOn`, `AtUrcOn` | [AtServer](AtServer.md), [AtClient](AtClient.md) |
| `AtServer.h` | `AtServer` | [AtServer](AtServer.md) |
| `AtClient.h` | `AtClient`, `AtQueue` | [AtClient](AtClient.md) |
| `AtDialect.h` | `AtDialect`, `AtCmeMode` | [AtDialect](AtDialect.md) |
| `AtError.h` | `AtStatus`, `AtCmeError`, the error messages | [AtClient](AtClient.md) |
| `Modules/AtEcho.h` | the client's echo module | [AtClient](AtClient.md) |
| `Modules/AtDataMode.h` | the data-mode tag (stage 2) | this page |

The argument declaration (`CommandArgument.h`), the name tree (`CommandNameTree.h`), the input ring (`CommandRing.h`) and the `String` type are shared with `Common/Utilities/CommandLine/`, which is not changed. The numbers are read and written with `Common/Utilities/TextNumber/`.

---

## Costs

- A set that lists no module, dialect item or URC handler has no code for them. A server with no dialect options has no echo, verbose, quiet, CME or register code. See [AtDialect](AtDialect.md) for the measured sizes.
- Each reply type costs its own code, since the handlers are template arguments.
- The ranges of a set's arguments are a table in flash, one entry for each argument. A five-command set with one URC (`SizeSet.h`), at -Os for Cortex-M4, has a server text of 6204 bytes and a client text of 4319 bytes.
- RAM, server: a 64-byte input ring, the largest command's values, and the URC queue (4 entries by default).
- RAM, client: a 64-byte input ring, a line buffer as long as the longest line of the set, and per queued request its text and its reply value.

---

## What Stage 2 Adds

Not in this part, and not in the description yet:

- `AtDataMode`: the `>` prompt, `Data()` URCs with raw counted bytes, `SendData`, and declared finals such as `SEND OK` after a prompt.
- `Rows`, `RowsVisit` and `AtRowWriter`: repeated info lines, such as `+CWLAP`, on both sides.
- `AtAwait`: `Await<"+X">()` for an RTOS task, with `WaitCondition`.
- `Descriptions/EspAt.h`: the first real description, checked against an ESP-AT release on hardware.

The extension points are marked in the code: the `AtDataModeTag` placeholder, the `AtIsClientModule` specializations for the client's module list, and the form kinds in `AtForms.h`.

---

## Tests

`Common/Utilities/AT/.tool/sim/run.sh` builds and runs everything on the host (`g++ -std=c++20 -Wall -Wextra -Werror`), checks the rules with the builder snippets, and compiles the sets for Cortex-M4 with `arm-none-eabi-g++` (set `ARM_CXX`).

- `TestAtDescription.cpp`, `TestAtValue.cpp`: the description and the value grammar.
- `TestAtServer.cpp`: the server, every form and argument type, chaining, the dialect, the input line end with the registers, CME with `CmeErrors(mode)` and `CmeCommand()`, and the URC queue.
- `TestAtRange.cpp`: the declared ranges on both sides: the edges of each range, one past each edge, an unsigned range, an optional range, the test text, a basic command's value, and the client's URC field and `Read` reply.
- `TestAtConsoleArguments.cpp`: a translation unit with the command line's console headers and the AT headers. Each one declares a set with `Argument<T>`, and each set runs its own command.
- `TestAtClient.cpp`: the client's encoding, its statuses, the URCs, the echo module, the queue, the drain and the timeouts, including the 32-bit wrap.
- `TestAtLoopback.cpp`: a server and a client from one description, joined by two in-memory wires, with one byte per chunk and with random chunks from fixed seeds.
- `TestAtTranscript.cpp` and `transcripts/*.txt`: hand-written modem sessions, fed whole, cut at every byte, and one byte at a time. The format is `> ` for a line the client writes, `< ` for a line the module sends, `@ ` for the time in milliseconds, and `# ` for a comment.
