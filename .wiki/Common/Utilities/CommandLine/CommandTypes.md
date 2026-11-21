# CommandTypes

`Common/Utilities/CommandLine/CommandTypes.h`: the types the declarations and the console share.

---

| Type | Meaning |
|---|---|
| `CommandArgumentKind` | `Integer`, `Boolean`, `Enumeration`, `Text`, `Custom` |
| `CommandFault` | Why an argument's text, or the command line, is refused: `None`, `Invalid`, `OutOfRange`, `TooLong`, `Missing` (an argument with no text and no default), `Extra` (an argument past the last), `MissingQuote` |
| `CommandEventKind` | The events: `Command`, `CommandFailed`, `UnknownCommand`, `ArgumentError`, `EmptyLine`, `Word`, `EndOfLine`, `Interrupt`, `TerminalOpened`, `TerminalClosed`, `InputOverflow` |
| `CommandEvent` | What the default handler gets: `kind`; `text` (the word of `UnknownCommand` and `Word`); `status` (of a failed command); `command` (the command an event is about); `argument` (the declared name of the argument of an `ArgumentError`), with its `argumentKind`, `fault`, and `minimum` and `maximum` (its range; `unsignedMaximum`, with `maximumUnsigned` set, holds a 64-bit unsigned maximum above the int64 range), and `truncated` on a `Word` whose word is longer than the word buffer. Fields that do not apply are empty or zero |
| `CommandCall<Ctx>` | What a handler gets first: `out` (a `Print&`) and `app` (a `Ctx&`) |
| `String<N>` | A string value: `characters` (N chars) and `length`. `View()` gives a `std::string_view` |
| `CommandSlot` | The reader interface: `Push(char)`, `Pop()`, `Finish(std::byte*) const`, and `Quoting() const`, which is false unless the reader is a quoted text |

`Print` is only forward-declared here, so the declaration side has no VHAL dependency beyond `ResultStatus` and the basic types.

`CommandSlot` has no virtual destructor. Readers live in storage the console owns, and they are never destroyed. The reader rule, from the header:

- `Push` returns `false` for a refused character. Later characters are refused too, until `Pop` removes the refused ones.
- `Finish` writes the value only when it returns `None`.
