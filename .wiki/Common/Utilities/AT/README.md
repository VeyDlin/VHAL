# AT

`Common/Utilities/AT/`: AT commands for embedded systems (VHAL), one description for both sides. A board answers AT commands for a host, and drives a module (Wi-Fi, a modem, a BLE dongle) with them. The description lists the commands, their forms, their argument types and their URCs. A server answers from it, and a client calls from it. No heap, no exceptions, no RTTI, no `std::function`, and no virtual calls in the AT code.

---

## Table of Contents

- [Quick Start](#quick-start)
- [The Description](#the-description)
- [Reply Forms](#reply-forms)
- [Rules Checked at Compile Time](#rules-checked-at-compile-time)
- [The Server](#the-server)
- [The Client](#the-client)
- [Headers](#headers)
- [Costs](#costs)
- [Modules](#modules)
- [Descriptions](#descriptions)
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
		.Command<"+CWLAP">().Rows(NetworkRow)                                     // AT+CWLAP   -> +CWLAP: (4,"net",-72)
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
AtClient<Sensor, ClientBinding, AtQueue<2>, AtEcho, AtRows<8>> sensor(out);
sensor.Feed(byte);                  // from the receive interrupt
sensor.Read<"+TEMP", &OnTemp>();    // queues the request; the reply comes in Process()
sensor.Process(nowMs);              // main loop
```

`App`, `Temp`, `Led`, `OnTemp`, `OnGotIp`, `NetworkRow` and `Rows` handlers are the application's own types and functions. The page for each side shows their signatures.

---

## The Description

`AtCommandSet<+[] { return AtCommandSetBuilder()...; }>` is built at compile time from a chain of calls. Each call returns a builder type that holds one more item, so the type of the chain carries every form and every argument.

| Call | Declares |
|---|---|
| `.Defaults(AtDefaults().TimeoutMs(n))` | the timeout of every command that does not give its own |
| `.Modules(tag)` | a set-level module. `AtDataMode` adds the `.Prompt()` form and the `Data()` URCs, see [AtDataMode](AtDataMode.md) |
| `.Dialect(AtDialect()...)` | the server's dialect options, see [AtDialect](AtDialect.md) |
| `.Command<"+X">()` | a command, with the forms and options that follow it |
| `.Basic<"X">()` | a basic command: one letter, whose value follows it with no `=`, as in `ATE0` |
| `.Urc<"prefix">(args...)` | a URC and its arguments |

Forms of a command, in the order a chain gives them:

| Form | Line | Declared by |
|---|---|---|
| test | `AT+X=?` | `.Test()` |
| read | `AT+X?` | `.Read<T>("name")`, `.Read(row)`, `.ReadRows(row)` |
| write | `AT+X=a,b` | `.Write(args...)` |
| run | `AT+X` | `.Run()`, `.Run<T>("name")`, `.Run(row)`, `.Rows(row)` |
| value (basic) | `ATX1` | `.Value(args...)` on a `.Basic<"X">()` |

A command has at most one read form and at most one run form. A second form of one kind is a compile error, so `.Run()` with `.Run<T>()` is refused.

Options of the open command: `.Reply("+CIFSR:STAIP")` (the info line's prefix, instead of the name and a colon), `.Finals("SEND OK")` (a final line that is a success), `.Failures("FAIL")` (a final line that is an error), `.TimeoutMs(n)`, and `.FailureCode("+CWJAP")` (the prefix of a code line such as `+CWJAP:2`, which comes before the error final; see [AtClient](AtClient.md#failure-codes)).

A required argument is `Argument<T>("name")`, the class the command line uses (`CommandArgument.h`), so a console and an AT set in one program name their arguments the same way. An optional argument is `Optional<T>("name")`, for a field that may be empty. Optional arguments come last.

| Type | On the line |
|---|---|
| `int8`, `int16`, `int32`, `int64` | signed decimal |
| `uint8`, `uint16`, `uint32` | unsigned decimal |
| `bool` | `0` or `1` only |
| `String<N>` | quoted text of at most N characters; `\"` and `\\` are the escapes |
| `RawString<N>` | unquoted text of at most N characters, to the end of the line. A reply value only, never an argument |

The server gets a `std::string_view` for a string argument and a `std::optional` for an optional one. The client takes the same types when it calls.

### Ranges and Defaults

- An integer argument takes its whole type. `.Range(low, high)` sets the values it accepts: `Argument<uint8>("level").Range(2, 9)`. A second `.Range()` replaces the first. `Optional<T>` takes `.Range()` the same way.
- The server refuses a value outside the range the way it refuses one that overflows its type: the reply is `ERROR`.
- The client drops a URC field outside the range, as it drops one that overflows. A `Read` reply is checked against its type only, since `Read` declares no argument.
- The client does not check the values it sends. A value outside the range is written as given, and the module decides.
- `.Default()` is refused in an AT description. An argument that may be absent is `Optional<T>`.
- An argument of an enum type, a float or another custom type is refused. An AT argument is an integer, `bool` or `String<N>`.

---

## Reply Forms

A reply is what a module sends back: a value, a record, or rows. Each one is declared on a read or run form, and the handler gets the reply in its type.

| Declaration | Reply | Client handler | Server handler |
|---|---|---|---|
| `.Read<T>("name")` | one value | `void(Result<T, AtStatus>)` | `Result<T, AtStatus>` |
| `.Run<T>("name")` | one value | `void(Result<T, AtStatus>)` | `Result<T, AtStatus>` |
| `.Read(row)`, `.Run(row)` | one record of fields | `void(Result<S, AtStatus>)` | `Result<S, AtStatus>` |
| `.ReadRows(row)`, `.Rows(row)` | one info line for each row | `void(const Result<AtRowList<S, N>, AtStatus>&)` | `AtStatus(AtCall, AtRowWriter<S>&)` |

- A record is one info line of fields, declared with `AtRow<S>(AtField<&S::member>("name")...)`. The fields, the Open and Close texts, and the rows are in [AtRows](AtRows.md).
- The rows of a list need the client module `AtRows<N>`, which holds N rows. Rows past N are counted and not stored.

### Raw Text

`RawString<N>` is unquoted text. As the value of a reply it is the rest of the line:

```cpp
.Command<"+GMR">().Run<RawString<64>>("version").Reply("AT version:")
// AT+GMR -> AT version: 2.2.0.0(Jul 1 2022)
```

On the server it is written as it is, up to the first CR or LF, so its text cannot end the line early. On the client it is read to the end of the line, and a longer text than N is `invalidData`. A `RawString` is never an argument, so `Optional<RawString<N>>` is a compile error.

### The Reply Separator

The info line of a reply is its prefix, then one separator, then the value:

| Reply prefix | Separator | Example |
|---|---|---|
| the name and a colon (no `.Reply()`) | one blank | `+TEMP: 23` |
| a prefix that ends in `:` | one blank | `AT version: 2.2.0.0` |
| any other prefix | one comma | `+CIFSR:STAIP,"192.168.1.9"` |

The server writes the separator of its prefix. The client skips one comma, when the prefix does not end in a colon, and then one blank, so it reads the value after either. A prefix that ends in a letter or a digit ends a word, so `+CIFSR:STAIP` does not take the line `+CIFSR:STAIP6LL`. A set with no reply prefix of the second kind has no code for the comma or the word check.

---

## Rules Checked at Compile Time

Each rule is a compile error with its message, as in the other VHAL builders. The snippets that check each one are in `Common/Utilities/AT/.tool/sim/BuilderErrors/`. The list covers the names (a command starts with `+` and has no space, a basic command is one letter, a name is declared once), the URC prefixes (no colon or comma, not a prefix of a command name or a reply prefix, declared once), the forms (once each per command, the value form only on a basic command), the arguments (an argument is an integer, `bool` or `String<N>` with no default, a required argument does not follow an optional one, no two names are the same, a string has a capacity above zero, a range has its low at most its high and both bounds fit the type), the options (a timeout above zero, `Defaults()` and `Dialect()` given once, a module is a module tag, a failure code prefix that is not empty, given once for a command, and neither a URC prefix nor a final line), the rows (see [AtRows](AtRows.md#rules-checked-at-compile-time)), the handlers (the signature of each server and client handler, and the name each one is bound to), and the client's modules (`AtRows` for `Rows` and `ReadRows`, and no `AtRows` with a capacity of 0).

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
| `AtValue.h` | the value grammar: readers and writers of every type, `RawString` | this page |
| `AtRow.h` | `AtField`, `AtRow`, the record and row declarations, and the server's `AtRowWriter` | [AtRows](AtRows.md) |
| `AtBinding.h` | `AtServerBinding`, `AtClientBinding`, `AtOn`, `AtUrcOn` | [AtServer](AtServer.md), [AtClient](AtClient.md) |
| `AtServer.h` | `AtServer` | [AtServer](AtServer.md) |
| `AtClient.h` | `AtClient`, `AtQueue` | [AtClient](AtClient.md) |
| `AtDialect.h` | `AtDialect`, `AtCmeMode` | [AtDialect](AtDialect.md) |
| `AtError.h` | `AtStatus`, `AtCmeError`, the error messages | [AtClient](AtClient.md) |
| `Modules/AtEcho.h` | the client's echo module | [AtEcho](AtEcho.md) |
| `Descriptions/EspAt.h` | the ESP-AT description and its two structs, `EspAtAccessPoint` and `EspAtJoinedNetwork` | [EspAt](Descriptions/EspAt.md) |
| `Modules/AtRows.h` | the client's row module `AtRows<N>` and the list `AtRowList` | [AtRows](AtRows.md) |
| `Modules/AtDataMode.h` | the data-mode module: `.Prompt()`, `Data()`, `SendData` and the chunk size | [AtDataMode](AtDataMode.md) |
| `Modules/AtAwait.h` | the client's await module: `AwaitRead`, `AwaitRun`, `AwaitWrite`, `AwaitValue`, `AwaitRows`, `AwaitReadRows` and `AwaitSendData` | [AtAwait](AtAwait.md) |

The argument declaration (`CommandArgument.h`), the name tree (`CommandNameTree.h`), the input ring (`CommandRing.h`) and the `String` type are shared with `Common/Utilities/CommandLine/`, which is not changed. The numbers are read and written with `Common/Utilities/TextNumber/`.

---

## Costs

- A set that lists no module, dialect item or URC handler has no code for them. A server with no dialect options has no echo, verbose, quiet, CME or register code. See [AtDialect](AtDialect.md) for the measured sizes.
- A set that declares no record and no row has no row code, and no reply separator code unless one of its reply prefixes does not end in a colon.
- Each reply type costs its own code, since the handlers are template arguments.
- The ranges of a set's arguments are a table in flash, one entry for each argument. A five-command set with one URC (`SizeSet.h`), at -Os for Cortex-M4, has a server text of 6144 bytes and a client text of 4267 bytes.
- `AtStatus` is 4 bytes: the status, the failure code (one byte) and the CME code. Every server and client returns it, in one register. A set without `.FailureCode()` has no code for the code line and no table of prefixes. A set with `.FailureCode()` keeps one table entry for each command that declares it.
- The ESP-AT client, at -Os for Cortex-M4 (text, data, bss; bss is 32 bytes in each): 11237 bytes with `AtEcho` and `AtRows<8>`, 12979 with `AtAwait` as well, and 9551 with neither module. The client object is 1160 bytes plain, 1752 with echo and rows, and 2792 with await. The sizes and the calls behind them are on the [EspAt](Descriptions/EspAt.md#sizes) page.
- RAM, server: a 64-byte input ring, the largest command's values, and the URC queue (4 entries by default).
- RAM, client: a 64-byte input ring, a line buffer as long as the longest line of the set, and per queued request its text and its reply value (a record counts its size). A client with `AtRows<N>` holds N rows.

---

## Modules

A module is a type in the set's list or the client's list. A module that is not listed adds no code. Each one has its own page.

| Module | Listed in | Adds | Costs when not listed | Page |
|---|---|---|---|---|
| `AtDataMode` | the set, `.Modules(AtDataMode)` | the `.Prompt()` form and `SendData`, `Data()` URCs, declared finals such as `SEND OK`, a 32-byte chunk buffer | nothing. A prompt command or a `Data()` URC without it is a compile error | [AtDataMode](AtDataMode.md) |
| `AtQueue<N>` | the client | the queue depth N. Each queued request holds its text and its reply | depth 2, the default | [AtClient](AtClient.md) |
| `AtRows<N>` | the client | one list of N rows, for `Rows` and `ReadRows` | nothing without a `Rows` or `ReadRows` call. Such a call without the module is a compile error | [AtRows](AtRows.md) |
| `AtAwait` | the client | the `Await` calls | nothing: no code, and no reference to `WaitCondition` or to the RTOS handle | [AtAwait](AtAwait.md) |
| `AtEcho` | the client | dropping the echo of a request | the flag is set only when the module is listed, so the compare never runs without it. Its code stays in the client (see [AtEcho](AtEcho.md#costs)) | [AtEcho](AtEcho.md) |

---

## Descriptions

Ready-made descriptions, in `Common/Utilities/AT/Descriptions/`:

| Description | Covers | Page |
|---|---|---|
| `EspAt.h` | the ESP-AT commands of `SocketAdapter` and `WiFiAdapter`: `AT+GMR`, `AT+RST`, `AT+CWMODE`, `AT+CWJAP`, `AT+CWQAP`, `AT+CWLAP`, `AT+CIFSR`, `AT+CIPMUX`, `AT+CIPSTART`, `AT+CIPSEND`, `AT+CIPCLOSE`, the `ATE` echo, and the ESP-AT URCs | [EspAt](Descriptions/EspAt.md) |

---

The ESP-AT description is in `Descriptions/EspAt.h`. Its timeouts and its reference values are marked "checked on the bench" on its page, see [EspAt](Descriptions/EspAt.md#bench-check).

The extension points are marked in the code: the `AtIsClientModule` specializations for the client's module list, and the form kinds in `AtForms.h`.

---

## Tests

`Common/Utilities/AT/.tool/sim/run.sh` builds and runs everything on the host (`g++ -std=c++20 -Wall -Wextra -Werror`), checks the rules with the builder snippets, and compiles the sets for Cortex-M4 with `arm-none-eabi-g++` (set `ARM_CXX`).

- `TestAtDescription.cpp`, `TestAtValue.cpp`: the description and the value grammar.
- `TestAtServer.cpp`: the server, every form and argument type, chaining, the dialect, the input line end with the registers, CME with `CmeErrors(mode)` and `CmeCommand()`, and the URC queue.
- `TestAtRange.cpp`: the declared ranges on both sides: the edges of each range, one past each edge, an unsigned range, an optional range, the test text, a basic command's value, and the client's URC field and `Read` reply.
- `TestAtConsoleArguments.cpp`: a translation unit with the command line's console headers and the AT headers. Each one declares a set with `Argument<T>`, and each set runs its own command.
- `TestAtClient.cpp`: the client's encoding, its statuses, the URCs, the echo module, the queue, the drain and the timeouts, including the 32-bit wrap. `Ping`: ok, error, a timeout with its drain, busy when the queue is full, and the echo dropped with `AtEcho`.
- `TestAtLoopback.cpp`: a server and a client from one description, joined by two in-memory wires, with one byte per chunk and with random chunks from fixed seeds. `Ping` across the wire, and a failure code that the server's handler returns, with the exact bytes the server writes.
- `TestAtReplies.cpp`: the reply forms on a loopback: a record read with an optional field, a record run with raw text, `Run<String<N>>` with its comma prefix, `Run<RawString<N>>` with its blank prefix, `Rows` of eight rows into a list of five (three skipped), `RowsVisit` of the same, `ReadRows` and `ReadRowsVisit`, a quoted `)` and `,` in a field, one byte per chunk and random chunks.
- `TestAtTranscript.cpp` and `transcripts/*.txt`: hand-written modem sessions, fed whole, cut at every byte, and one byte at a time. The format is `> ` for a line the client writes, `< ` for a line the module sends, `@ ` for the time in milliseconds, and `# ` for a comment. The `failure_code_*.txt` sessions cover a code line with and without a blank, an OK after it, a line with no integer, 256 and 255, a URC between the code line and ERROR, and a read form that takes the line as its value.
- `TestAtReplyTranscript.cpp` and `transcripts/cifsr_stap.txt`, `cifsr_ipv6.txt`, `gmr_raw.txt`, `rows_urc_between.txt`, `rows_bad_row.txt`: the reply transcripts, in the same three ways. A URC between two rows, the STAIP line among other address lines and before its IPv6 lines, a raw version with its SDK lines, the extra and quoted fields of a row, rows past the capacity of two, and a row without a required field.
- `TestAtAwait.cpp`: the Await calls with a scripted module that runs on the RTOS handle: each form's value and status, a request queued behind another, `busy` when full, a module timeout across the 32-bit wrap, the safety limit with and without a sent request, and `AwaitPing`. See [AtAwait](AtAwait.md#tests).
- `TestAtData.cpp`: the data mode, with a client and a server of one set: the prompt and its data, a request that waits for the first, `busy`, the chunks of a `Data()` run, and the server's `ERROR` for a prompt command. The transcripts `send_data_*.txt`, `ipd_*.txt` and `gt_outside_prompt.txt` are in `TestAtTranscript.cpp`, see [AtDataMode](AtDataMode.md#tests).
- `TestAtEspAt.cpp` and `transcripts/espat_*.txt`: the ESP-AT description. A client with `AtEcho`, `AtRows<8>` and `AtAwait`, and a server bound to every form except the prompt, both compile. Eighteen hand-written ESP-AT sessions, written from the reference, are fed whole, cut at every byte, and one byte at a time. See [EspAt](Descriptions/EspAt.md#tests).
- `SizeEspAtRows.cpp`, `SizeEspAtAwait.cpp` and `SizeEspAtPlain.cpp`: the three ESP-AT client objects that `run.sh` measures for the sizes above.
