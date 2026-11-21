# CommandAnsi

`Common/Utilities/CommandLine/CommandAnsi.h`: the `Ansi` module. The console writes ANSI escape sequences for colour and for the screen: a coloured prompt, red errors, and Ctrl+L to clear the screen. Listed after `Help`: `CommandLine<Set, App, Help, LineEditing<64>, Ansi>`.

---

## Output

| Event | With Ansi |
|---|---|
| Prompt | `ESC[32m> ESC[0m`: a green `> ` |
| Standard error (CommandFailed, UnknownCommand, ArgumentError, InputOverflow) | `ESC[31m`, the standard text, then `ESC[0m` |
| Ctrl+L (0x0C) | `ESC[2J ESC[H`, the prompt, then the line from `LineEditing`, with the cursor moved back to where it was |

- The standard texts are the ones CommandMessages.h writes. A set with its own `Default()` prints its own texts, so `Ansi` colours none of them. The prompt is still green.
- Ctrl+L without `LineEditing` writes the prompt only, since the console keeps no text of the line.
- A console without `Ansi` writes no escape sequence. Ctrl+L is ignored, as any control character is.

---

## Rules

- `Ansi` takes characters, keys and events, so `Help` must be listed before it. A compile error otherwise (see CommandModules.md).
- The prompt hook returns `true`, so the console does not write its own `> ` after it. See CommandModules.md.
- The escape sequences of the input are read and dropped as before: an arrow key that no module uses injects nothing.

---

## Structure

| Part | Use |
|---|---|
| `struct Ansi` | The module. `OnCharacter` takes Ctrl+L, `OnEvent` colours the standard errors (only in a set without `Default()`), and `OnPrompt` writes the green prompt |
| `CommandIsErrorEvent` | Whether an event is one of the standard errors (CommandMessages.h) |
| `CommandWriteEvent<Set>` | The standard text, written with the set (CommandMessages.h) |

The module has no state: it costs one byte in the console's module pack.
