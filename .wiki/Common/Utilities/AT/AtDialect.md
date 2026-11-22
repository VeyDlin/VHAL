# AtDialect

`Common/Utilities/AT/AtDialect.h`: the options of the server's dialect. Each option is added with `AtDialect()` in the description and costs code only when it is there.

---

## Table of Contents

- [Options](#options)
- [Input Line End](#input-line-end)
- [Defaults](#defaults)
- [CME Errors](#cme-errors)
- [Limits and open points](#limits-and-open-points)
- [Costs](#costs)

---

## Options

```cpp
.Dialect(AtDialect().Echo().Verbose().Quiet().Registers().CmeErrors(AtCmeMode::verbose).CmeText<3, "operation not allowed">())
```

| Option | Adds | Behaviour |
|---|---|---|
| `.Echo()` | `ATE`: the server echoes each byte it reads | `ATE0` turns it off, `ATE1` on, `ATE` alone is 0. A client sends `ATE0` with `Value<"E", &Done>(uint8{0})` |
| `.Verbose()` | `ATV` | `ATV1` gives words (`OK`, `ERROR`), `ATV0` gives numbers (`0`, `4`) |
| `.Quiet()` | `ATQ` | `ATQ1` suppresses the result codes. Info lines are still sent |
| `.Registers()` | `ATS3`, `ATS4` | the output line end (S3) and line feed (S4), read and written at run time. S3 also ends an input line, see [Input Line End](#input-line-end) |
| `.CmeErrors(mode)` | the CME mode | `off` gives `ERROR`, `numeric` gives `+CME ERROR: n`, `verbose` gives `+CME ERROR: text`. Alone, the mode is fixed. With `.CmeCommand()`, it is the mode at start |
| `.CmeCommand()` | `AT+CMEE`, the CME mode at run time | `AT+CMEE=0`, `1`, `2`; `AT+CMEE?` and `AT+CMEE=?`. Alone, the mode starts at 0 |
| `.CmeText<code, "text">()` | the verbose text of one CME code | used only with `verbose` |

Bare `AT`, `;` chaining and concatenated basic commands are always on. They are part of the forms, and cost little. A basic command's value runs to the next letter or `+`, so `ATE0V1` is `ATE0` and then `ATV1`, as V.250 gives.

`.CmeErrors(mode)` and `.CmeCommand()` can be given together, in either order. `CmeErrors` then gives the mode at start, and `AT+CMEE` changes it at run time. `.CmeErrors(mode)` alone is fixed, and the set has no `AT+CMEE`. Each `.CmeText<>()` is a code, and the text is kept only for the codes the set declares.

`ATS3`, `ATS4` and `ATS3=n` (0 to 127, as V.250 gives) are the only registers. `ATS3?` gives three digits. An unknown register, or `ATSn` with neither `?` nor `=`, is an error.

The builtin names share the command tree with the set's commands: `E`, `V`, `Q`, `S` and `+CMEE`. A basic command with the same letter as a dialect option is a compile error.

---

## Input Line End

Without `.Registers()`, a line ends at CR or LF, and CRLF or LFCR is one end.

With `.Registers()`, a line ends at the current S3 character, so `ATS3=35` makes `#` the line end. While S3 is 13, LF also ends a line, and CRLF or LFCR is one end. While S3 is any other value, a CR or LF that is not S3 is dropped, so a host that keeps sending CRLF still has its commands read. A line that is not ended, because it has no S3 character, is not answered until one comes.

For example, after `ATS3=35`, the line `AT+TEMP?` followed by CR is not answered yet. Sending `#` then answers it, and `AT+TEMP?#` is answered at once.

S3 and S4 also set the characters the server writes.

## Defaults

With the options present, the values the server starts with are:

| Setting | Default |
|---|---|
| echo (`ATE`) | on (`ATE1`) |
| verbose (`ATV`) | on (`ATV1`) |
| quiet (`ATQ`) | off (`ATQ0`) |
| S3 (line end) | 13 (CR) |
| S4 (line feed) | 10 (LF) |
| CME mode | the `.CmeErrors(mode)` mode when it is given, which `AT+CMEE` can change. `off` with `.CmeCommand()` alone, which sets it at run time from 0 |

An empty value of `ATE`, `ATV` or `ATQ` is 0, as V.250 gives. Only 0 and 1 are valid values, and any other value is an error.

---

## Limits and open points

- **Echo owns `E`.** With `.Echo()`, a basic `E` command cannot also be declared.
- **Unknown verbose text.** A client that gets `+CME ERROR: <text>` with a text that is not in its set's table gets `error` with code 0. The code is lost, since the text is not kept.
- **Numeric codes are not read by the client.** With `ATV0` the server writes `0` and `4`. The client of this library does not parse them, so a request answered with numbers times out, and its drain ends at its own timeout. Keep `ATV1` on a link whose client is this library.
- **Echo is not in the client's control.** The client never sends `ATE` by itself. The application sends `ATE0` or `ATE1` with `Value<"E">`, and the client's `AtEcho` module drops the echo only when it is listed (see [AtEcho](Modules/AtEcho.md)).

---

## CME Errors

With `.CmeErrors(AtCmeMode::verbose)` a handler that returns `AtCmeError(3)` gives `+CME ERROR: operation not allowed`. The text comes from `.CmeText<3, "...">()`. A code with no text gives its number.

With `.CmeErrors(AtCmeMode::numeric)` and `.CmeCommand()` together, the set starts in numeric mode. `AT+CMEE?` reports 1, and after `AT+CMEE=0` the same handler's error is a plain `ERROR`.

The client reads both forms, so a `+CME ERROR:` line gives the same status whichever mode the module is in.

---

## Costs

The sizes below are the server of a five-command set with every dialect option, and the same set with no dialect option. They are `arm-none-eabi-size` on the `-Os` objects for Cortex-M4, from the `run.sh` script.

| Object | text | data | bss |
|---|---|---|---|
| no dialect option | 3147 | 4 | 32 |
| every dialect option | 4940 | 4 | 32 |

The dialect adds 1793 bytes of text. The `data` and `bss` figures are the simulation's own globals, and are the same in both.

Symbols that appear only with the dialect: the builtin executor, the register and CME executors, the switch setter, the builtin info line writer, the builtin list and the CME text table. Each option's share of the text is not measured separately.
