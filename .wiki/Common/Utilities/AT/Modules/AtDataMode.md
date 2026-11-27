# AtDataMode

`Common/Utilities/AT/Modules/AtDataMode.h`: the data-mode module. Some commands carry raw bytes: a host sends a payload after a `>` prompt, and a module sends a payload inside a URC, with a length in front of it. A line ends at CR or LF, so raw bytes cannot be read as lines. The module gives a set two things for them: a prompt command, and a URC with `Data()`. A set that does not list the module has neither, and no code or RAM for them.

---

## Table of Contents

- [Declaration](#declaration)
- [Prompt Commands](#prompt-commands)
- [Data URCs](#data-urcs)
- [The Server](#the-server)
- [Rules Checked at Compile Time](#rules-checked-at-compile-time)
- [Costs](#costs)
- [Tests](#tests)

---

## Declaration

```cpp
using Modem = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Defaults(AtDefaults().TimeoutMs(2000))
		.Modules(AtDataMode)
		.Command<"+CIPSEND">().Write(Argument<uint16>("length")).Prompt().Finals("SEND OK").Failures("SEND FAIL")
		.Command<"+CIPSTART">().Write(Argument<String<8>>("type"), Argument<String<32>>("host"), Argument<uint16>("port")).TimeoutMs(10000)
		.Urc<"+IPD">(Argument<uint16>("length"), Data());
}>;
```

`.Modules(AtDataMode)` is in the set. A client and a server of this set are built as the other sets are.

---

## Prompt Commands

`.Prompt()` follows the write form of a command. It says that the module answers the request with `>` and waits for the data, and that the client sends the data after the prompt. `Write` on a prompt command is a compile error. The client sends its data with `SendData`:

```cpp
void OnSent(AtStatus status) {
	// ok after SEND OK, error after SEND FAIL, ERROR or +CME ERROR
}

static constexpr uint8 kPayload[] = {'h', 'e', 'l', 'l', 'o'};

modem.SendData<"+CIPSEND", &OnSent>(uint16{5}, std::span<const uint8>(kPayload));   // AT+CIPSEND=5, then the five bytes
```

The values before the data are the values of the write form, and they are checked as `Write` checks them. The last value is the data, a `std::span<const uint8>`. The span is **not copied**: it must stay valid until the handler runs, so use static storage or a buffer that the request's owner keeps.

The request goes through the queue like any other. Its steps:

1. The request line is written: `AT+CIPSEND=5`.
2. Before the prompt, `OK` does not end the request, since the module sends `OK` and then the prompt. `ERROR`, `+CME ERROR` and the declared failures do.
3. A `>` at the start of a line, with no line end needed, makes `Process()` write the data bytes. One blank after the `>` is skipped, as in `> `. A `>` in the middle of a line is an ordinary character, and a `>` while no prompt request waits for it is an ordinary line too.
4. After the prompt, the declared finals and failures end the request, with `ERROR` and `+CME ERROR` too. A declared final such as `SEND OK` is the success.
5. Unknown lines, such as `Recv 5 bytes`, and URCs are handled as usual.

The command timeout covers the whole exchange, from the request line to the final line. A timeout drains the request as any other timeout does (see [AtClient](../AtClient.md#timing-queue-and-drain)). A prompt command must declare a final line, such as `SEND OK`, because without one the request could never end after its data.

---

## Data URCs

A URC whose last argument is `Data()` carries raw bytes. The argument before `Data()` is the length, an unsigned integer (`uint8`, `uint16` or `uint32`), and it is required. After the length comes `:`, and then exactly that many raw bytes. The bytes never form lines: CR and LF inside them are data.

```cpp
void OnIpd(uint16 length, uint16 offset, std::span<const uint8> chunk, bool last) {
	// length is the run's length; offset is where this chunk starts in the run; chunk holds the bytes; last is set on the final chunk
}

using Binding = AtClientBinding<AtUrcOn<"+IPD", &OnIpd>>;
```

- The handler gets the arguments before `Data()`, then the chunk's offset (the type of the length), the chunk as a `std::span<const uint8>`, and whether the chunk is the last one.
- A run is delivered in chunks of at most 32 bytes (`AtChunkBytes`). The chunks come in order, the offsets add up, and only the last chunk has `last` set. The chunks do not depend on how the bytes were split across `Feed` and `Process`.
- A length of 0 gives one call, with an empty chunk and `last` set.
- A header whose length does not fit its type is refused: no bytes are taken as data, and the line is dropped as an ordinary URC line, the same as one with a missing field.
- A `Data()` URC can arrive while a request is open, also between the prompt and `SEND OK`. It does not end that request, and the request's reply is not disturbed.
- The handler is called from `Process()`, as the other handlers are.
- A run is counted, not framed. The client takes exactly `length` bytes after the header, whatever they are. Nothing ends a run early: a request timeout does not end it, and a byte lost to a full receive ring (counted in `droppedBytes`) or a module that sends fewer bytes than its length keeps the run open until the count is met. The lines after such a run are then read as data, so a lost byte inside a run is not recovered.

---

## The Server

The server in version 1 answers a prompt command with `ERROR`. A prompt command can have no handler: binding one is a compile error with its message. So one description with prompt commands and `Data()` URCs still builds a server, and each prompt command gets `ERROR`.

A server URC with `Data()` is not in version 1. `server.Urc<"+IPD">(...)` with a `Data()` URC is a compile error, since the server would have to send raw bytes that a queue does not hold.

---

## Rules Checked at Compile Time

Each rule is a compile error with its message. The snippets for each one are in `Common/Utilities/AT/.test/BuilderErrors/`.

| Rule | Message |
|---|---|
| `Prompt()` on a form other than a write form, or before the write form | `Prompt() is for a write form, which comes before it` |
| `Prompt()` given twice | `Prompt() is given twice` |
| a prompt command in a set without `AtDataMode` | `a prompt command needs AtDataMode among the modules of the set` |
| a prompt command with no declared final | `a prompt command needs a declared final, such as SEND OK, to end after its data` |
| a URC with `Data()` in a set without `AtDataMode` | `Data() in a URC needs AtDataMode among the modules of the set` |
| `Data()` not the last argument | `Data() must be the last argument of a URC` |
| `Data()` with no argument before it | `Data() needs a length argument before it` |
| a length that is not a required `uint8`, `uint16` or `uint32` | `the length before Data() must be a required uint8, uint16 or uint32 argument` |
| a client `Write` on a prompt command | `a prompt command takes SendData, not Write` |
| a client `SendData` on a command without `.Prompt()` | `SendData is for a command with .Prompt()` |
| a client `SendData` in a set without `AtDataMode` | `SendData needs AtDataMode among the modules of the set` |
| `SendData` values that do not match the write form, with the data last | `SendData is given values that do not match the arguments of its write form, then the data as std::span<const uint8>` |
| a server handler bound to a prompt command | `a prompt command has no server handler: the server answers its line with ERROR` |
| a server `Urc<>()` of a URC with `Data()` | `a server URC with Data() is not in v1: the server writes no raw data` |
| a client URC handler with the wrong signature for `Data()` | `a URC handler does not match the arguments that the description declares for it` |

---

## Costs

- A set without `AtDataMode` has no prompt code, no data code and no chunk buffer. The five-command set of `SizeSet.h`, which has no data, is unchanged by this module: at -Os for Cortex-M4 its server text is 6144 bytes and its client text is 4267 bytes.
- With the module, the client holds one chunk buffer of 32 bytes, and a few bytes of state for the run and the prompt.
- Each queued `SendData` request keeps its data span (a pointer and a length, 8 bytes on a 32-bit MCU) and whether its prompt has come, about 12 bytes in the request's slot. A client without the module keeps neither.
- The server holds no data state. A prompt command costs the server its write form's argument parsing and an `ERROR` line.

---

## Tests

- `TestAtTranscript.cpp` with the transcripts `send_data_urc_between_prompt_and_ok.txt`, `send_data_fail.txt`, `send_error_before_prompt.txt`, `send_timeout_during_prompt_wait.txt`, `ipd_cr_lf_in_data.txt`, `ipd_length_zero.txt`, `ipd_back_to_back.txt`, `ipd_between_info_lines.txt`, `ipd_chunks_of_32.txt`, `ipd_length_overflow.txt` and `gt_outside_prompt.txt`. Each one is fed whole, cut at every byte, and one byte at a time. A raw run is written in the transcript as `= ` followed by hex, and the client's data as `> = `.
- `TestAtData.cpp`: the prompt path (the data after the prompt and no earlier, the prompt only at a line start, the second request waiting for the first), `busy` when the queue is full, a zero-length request, the chunks of a 70-byte run with CR and LF inside, a length-zero run, the refused overflow, a run during a read, and the server's `ERROR` for a prompt command with its queued URC after it.
- `TestAtEspAt.cpp` with `espat_cipsend.txt` (the prompt, five bytes, `Recv 5 bytes` and `SEND OK`) and `espat_cipsend_ipd.txt` (a `+IPD` with three bytes between the data and `SEND OK`). The ESP-AT description has `AtDataMode` and a prompt command, see [EspAt](../Descriptions/EspAt.md).
- The builder snippets listed above.
