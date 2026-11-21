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

## Process, in order

1. `SyncTerminal`: if the terminal state changed, the parser and the input are reset, and `TerminalOpened` (with the greeting and a prompt) or `TerminalClosed` is raised.
2. If the ring lost bytes since the last call, `InputOverflow` is raised.
3. Before each byte, a refused Ctrl+C whose place the ring has reached cancels the line, as `Interrupted` does below (see Ctrl+C).
4. Each byte of the ring is handled: `Input.Step` turns it into a character, a line end or a key.

A character goes to the modules' `OnCharacter` first. Then Ctrl+C cancels the line. Backspace (`0x08`, `0x7F`) erases through the parser, and echoes `BS SPACE BS` only when something was erased. A tab is read as a space. Other bytes outside `0x20`–`0x7E` are ignored. Any other character is echoed and given to the parser.

A line end first echoes `\r\n`, then calls each module's `OnLineEnd`. A module that returns true takes the line, and no events are raised for it. Otherwise the line is evaluated: in command mode, `EndCommandLine` runs the command or raises the error. In word mode (default-only set), `EndWordLine` raises `Word`, `EndOfLine` or `EmptyLine`. The parser is then reset and the prompt is written.

---

## Events

`Raise(event)` gives an event to the modules' `OnEvent` hooks, in order, until one takes it. A module that takes none, or no module, leaves it to the set: `CallDefault` when the set has a `Default()`, otherwise `CommandWriteEvent` (CommandMessages.h). `RaiseMessage` makes an event with only a kind.

A command's `ResultStatus` other than `ok` is raised as `CommandFailed` with the command's name. A status is only raised for a command that ran, not for an error in its line.

---

## Ctrl+C

`Feed` calls `Set::CallInterrupt(app)` when the byte is `CommandInterruptByte` (`0x03`), and only when the set has an `OnInterrupt()`. The byte is then pushed like any other, so `Process` reads it at its place in the line, and `Interrupted` cancels the line, raises `Interrupt` and writes the prompt. If the ring is full, the Ctrl+C is refused and counted as lost. The ring records its place, after the bytes accepted so far. `Process` handles those bytes, echoed and parsed as usual, and then cancels the line at that place. A Ctrl+C refused while an earlier one still waits takes the later place.

---

## Limits

- The console is one object of its set and its modules, with no heap. Its size is in README.md, Memory.
- `Commands` is the set type, so a module can name the set's operations: `typename Console::Commands::TreeStep`.
- A set with no commands and no groups runs the word path (`Set::EntryCount == 0`), which has no tree.
