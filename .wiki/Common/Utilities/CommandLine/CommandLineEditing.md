# CommandLineEditing

`Common/Utilities/CommandLine/CommandLineEditing.h`: the `LineEditing<N>` module. It keeps the line in a buffer of `N` bytes with a cursor, so that the keys can move in it and change it. Listed after the application type: `CommandLine<Set, App, Help, LineEditing<64>>`.

For the overall console, see README.md.

---

## Keys

| Key | Action |
|---|---|
| Left, Right | Move the cursor one character |
| Home, Ctrl+A | Move the cursor to the start |
| End, Ctrl+E | Move the cursor to the end |
| Delete (`CSI 3 ~`) | Delete the character at the cursor |
| Backspace, DEL | Delete the character before the cursor |
| Ctrl+K | Delete from the cursor to the end |
| Ctrl+W | Delete the spaces and the word before the cursor |
| A printable character | Insert it at the cursor |

Home and End also come as `CSI 1 ~` and `CSI 4 ~`, and as `CSI H` and `CSI F`. A character that does not fit the buffer is refused with a bell (`0x07`) and neither echoed nor parsed.

---

## The screen

The module writes only plain sequences:

| Move | Text |
|---|---|
| n characters left | `CSI n D`, for n above zero |
| n characters right | `CSI n C`, for n above zero |
| Clear to the end of the line | `CSI K` |
| Backspace at the end | `BS SPACE BS`, the same as without the module |

An edit before the end rewrites the tail. The cursor goes to the edit's start, the tail is written, the rest of the line is cleared, and the cursor goes back to where the edit leaves it. Ctrl+K at the cursor writes only `CSI K`.

---

## The buffer and the parser

- Typing at the end is given to the parser one character at a time, and echoed, as without the module. Ctrl+W at the end erases through the parser one character at a time, too.
- An edit before the end (an insert, a delete, Ctrl+K or Ctrl+W with a tail) changes the buffer, then replays the whole buffer through the parser, without echo and without events. So the parser always holds the line as the buffer shows it, and the handler gets the edited values at Enter.
- A cursor move alone does not touch the parser.

Replay raises no `Word` events in a default-only build, and neither does typing. A default-only build with `LineEditing` raises its `Word` events at Enter, from the buffer, then `EndOfLine`:

- The buffer is read from its start through the console's parser, after a reset, and each `Word` event is raised as the streamed path raises one: when the next word starts, or at Enter for the last. So the splitting, and the `truncated` flag of a long word, are the same as without the buffer.
- Backspace at the end erases from the buffer, and echoes `BS SPACE BS`. It is not limited by the parser, which cannot go back over a word that has ended, so Backspace can go back into an earlier word.
- Example: `ab cd`, then Home, then `x`, then Enter gives `Word` `xab`, `Word` `cd`, `EndOfLine`. `ab cd` with four Backspaces, then Enter, gives `Word` `a` and `EndOfLine`.
- Without `LineEditing`, a default-only build raises each `Word` as the line is typed. Once a later word has started, the earlier one is final, as `CommandLine.md` describes.

---

## Lifecycle

- The buffer is cleared when the prompt is written after a line end, so the modules at the line end (Help, History) read the whole line first.
- It is cleared when the terminal opens or closes, and when Ctrl+C cancels the line.
- Tab is not taken here. Completion takes it. With no module taking it, a Tab is typed as a space through the modules, so the buffer keeps it.

---

## Hooks

- `OnCharacter`: Backspace, DEL, Ctrl+A, Ctrl+E, Ctrl+K, Ctrl+W and the printable characters are taken (true). Other bytes, Tab and Ctrl+C are not (false).
- `OnKey`: Left, Right, Home, End and Delete are taken. Up and Down are not, so History takes them.
- `OnLineEnd`: marks the line as ended, and takes nothing.
- `OnEvent`: clears the buffer on `Interrupt`, `TerminalOpened` and `TerminalClosed`. Takes nothing.
- `OnPrompt`: clears the buffer when the line has ended.
- Redraws go through `Echo`: a line that holds a `Secret` (HiddenInput) draws its tail as `*`. The buffer is replayed before the tail is drawn, so an insert of a secret character is masked too.

---

## Feature and entry points

`Provides` is `CommandLineBufferFeature`. Help reads the line through it when it is listed. History edits the line through it (`Replace`). Completion moves the cursor to the end through `ToEnd`.

| Member | Use |
|---|---|
| `Text()` | The line as it stands |
| `ToEnd(console)` | Move the cursor to the end, with its output |
| `Replace(console, text)` | Put `text` in the buffer, up to `N` characters, draw it and replay it. History recalls with this |
| `Redraw(console)` | Writes the line after the prompt and moves the cursor back to its place in the buffer. Ansi uses it after Ctrl+L |

---

## Limits

- The line is at most `N` characters. A longer line is refused character by character with a bell.
- The output does not wrap. A line longer than the terminal's width is not redrawn correctly. The sequences assume one line on the screen.
- Mid-line edits (an insert, a delete, Ctrl+K or Ctrl+W with a tail, and a Backspace before the end) redraw correctly only while the prompt and the line fit the terminal's width. Past that width the cursor moves (`CSI n D` and `CSI n C`) count screen columns that wrap, so the redraw lands in the wrong place.
- The console does not query the terminal's width, so it cannot tell when a line wraps. A user must keep the prompt and the line within the terminal's width for a mid-line edit to redraw.
- Host size: 88 bytes for `LineEditing<64>` (the buffer, the length, the cursor and the ended flag). Not measured on the G431.
