# CommandLine

`Common/Utilities/CommandLine/CommandLine.h`: the console. `CommandLine<Set, App, Modules...>` reads bytes, parses them as they arrive, raises events and runs handlers.

```cpp
CommandLine<AppCommands, App, Help> console(app, out);

console.Feed(byte);             // from the receive interrupt or the main loop
console.Process();              // from the main loop: parses, raises events, runs handlers, prints
console.SetTerminalOpen(dtr);   // the greeting and the prompt come from the next Process
```

For the whole picture, and the events, see README.md.

---

## Public interface

| Member | Use |
|---|---|
| `using Commands = Set;` | The command set, for modules that look up commands (Help) |
| `CommandLine(App&, Print&)` | Binds the application and the output. Checks the handler contexts at compile time (`Set::CheckContext<App>()`) |
| `Feed(uint8)` | Stores a byte. Calls the `OnInterrupt` handler first when the byte is Ctrl+C. Safe to call from an interrupt |
| `Process()` | Drains the ring and raises the events. Call it from the main loop or a task |
| `SetTerminalOpen(bool)` | Stores the terminal state (atomic). The events come from the next `Process` |
| `Output()`, `Application()` | The `Print&` and the `App&`, for modules |

The input ring is 32 bytes, or the size of an `InputRing<N>` listed among the types: `CommandLine<Set, App, InputRing<64>, Help>`. A console lists at most one. The modules are listed after `App`, and a module's `Needs` is checked by a `static_assert` (see CommandModules.md).

---

## For modules

These are public so that the editing modules can work on the line. An application does not need them.

| Method | Use |
|---|---|
| `Type(char)` | Types a character through the modules, as if it came from the input. A Tab is typed as a space through the modules, when nothing takes it |
| `Append(char)` | Parses and echoes a printable character at the end of the line, with no modules. In a default-only build without `LineEditing`, it raises the `Word` the character ends |
| `EraseLast()` | Removes the last character, through the parser, and echoes the erase. Returns false when nothing was there |
| `Replay(text, size)` | Parses the text again from the start, with no echo and no events. The module draws the screen itself |
| `RedrawPrompt()` | Writes the prompt and the modules' prompt hooks again |
| `Echo(text)` | Writes text the line shows, for a module that redraws it. A line that holds a secret shows `*` for each character |
| `HoldsSecret()` | Whether the line holds a `Secret` argument's text (HiddenInput). `History` does not keep such a line |
| `Changes()` | A count of the changes to the line. Completion uses it to tell whether the line changed |
| `Parsed()` | The parser, read only. Completion reads the token at the end with `EditPoint()` |
| `WithModule<Feature>(visitor)` | Calls the visitor with the first listed module that provides `Feature`. Returns false when none does |

The change count is also raised by a line end and by Ctrl+C, so a new line never looks like an old one.

## Process, in order

1. `SyncTerminal`: if the terminal state changed, the parser and the input are reset, and `TerminalOpened` (with the greeting and a prompt) or `TerminalClosed` is raised.
2. If the ring lost bytes since the last call, `InputOverflow` is raised.
3. Before each byte, a refused Ctrl+C whose place the ring has reached cancels the line, as `Interrupted` does below (see Ctrl+C).
4. Each byte of the ring is handled: `Input.Step` turns it into a character, a line end or a key.

A character goes to the modules' `OnCharacter` first. Then Ctrl+C cancels the line. Backspace (`0x08`, `0x7F`) erases through the parser, and echoes `BS SPACE BS` only when something was erased. In a default-only build with `LineEditing`, the buffer decides what is erased, since the parser cannot go back over an ended word. A tab that no module takes is typed as a space, through the modules again, so a module that keeps the line sees the space. Other bytes outside `0x20`–`0x7E` are ignored. Any other character is echoed and given to the parser.

A line end first echoes `\r\n`, then calls each module's `OnLineEnd`. A module that returns true takes the line, and no events are raised for it. Otherwise the line is evaluated: in command mode, `EndCommandLine` runs the command or raises the error. In word mode (default-only set), `EndWordLine` raises `Word`, `EndOfLine` or `EmptyLine`. Without `LineEditing`, the parser has raised each `Word` as the line was typed, and `EndWordLine` raises the last one. With `LineEditing`, nothing is raised while typing: `EndWordLine` reads the buffer again from its start (`ReplayWords`), raises each `Word` as the streamed path would, then `EndOfLine`. The parser is then reset and the prompt is written.

---

## Events

`Raise(event)` gives an event to the modules' `OnEvent` hooks, in order, until one takes it. A module that takes none, or no module, leaves it to the set: `CallDefault` when the set has a `Default()`, otherwise `CommandWriteEvent` (CommandMessages.h). `RaiseMessage` makes an event with only a kind.

A command's `ResultStatus` other than `ok` is raised as `CommandFailed` with the command's name. A status is only raised for a command that ran, not for an error in its line.

---

## Ctrl+C

`Feed` calls `Set::CallInterrupt(app)` when the byte is `CommandInterruptByte` (`0x03`), and only when the set has an `OnInterrupt()`. The byte is then pushed like any other, so `Process` reads it at its place in the line, and `Interrupted` cancels the line, raises `Interrupt` and writes the prompt. If the ring is full, the Ctrl+C is refused and counted as lost. The ring records its place, after the bytes accepted so far. `Process` handles those bytes, echoed and parsed as usual, and then cancels the line at that place. Each refused press has its own place, after the bytes accepted before it, and raises its own Interrupt when `Process` reaches that place. Two refused presses at the same place give two Interrupts, one after the other, and the second `^C` comes with a new prompt. At most two refused presses wait at once. A third one, refused while two wait, is dropped: it raises no Interrupt and cancels no line, though an `OnInterrupt` handler still runs.

A long command (LongCommands) keeps the parser's line open. `Process` then runs its next step once per pass, before it reads any input. While the command runs, the input stays in the ring. A refused Ctrl+C cancels the command at once, even though the command does not reach its place: `TakeWaitingInterrupt` drops the bytes before its place. A second refused Ctrl+C that waits stays for its own place. A Ctrl+C in the ring, before any waiting press, cancels the command too: `DiscardTo` drops the bytes before it. If that Ctrl+C lies before a waiting press, the press keeps its place and its own `Interrupt`. Either way the handler gets its cancel call, and the `Interrupt` event follows. When the command ends, `Process` goes on reading the input that waited. See CommandLongCommands.md.

---

## Limits

- The console is one object of its set and its modules, with no heap. Its size is in README.md, Memory.
- `Commands` is the set type, so a module can name the set's operations: `typename Console::Commands::TreeStep`.
- A set with no commands and no groups runs the word path (`Set::EntryCount == 0`), which has no tree.
