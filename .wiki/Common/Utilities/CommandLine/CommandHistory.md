# CommandHistory

`Common/Utilities/CommandLine/CommandHistory.h`: the `History<Lines, Bytes>` module. The Up and Down keys recall the lines the console read before. Listed after `LineEditing`: `CommandLine<Set, App, Help, LineEditing<64>, History<4, 128>>`. Without `LineEditing` the console does not compile, with `a module needs a feature that no listed module provides`.

---

## Rules

- `Lines` is the most entries held, and `Bytes` is the size of the ring they are kept in. An entry takes its length plus two bytes, which hold its length at both ends, so the ring can be walked backwards.
- A line longer than 255 characters is not kept. An empty line is not kept. A line that repeats the newest entry is not kept.
- A recalled line is put in the `LineEditing` buffer and replayed through the parser. It can be edited, and Enter runs it. Enter then keeps it, if it is not a repeat.
- The first Up keeps the line being typed as the newest entry, the draft. While a recall lasts, the draft takes one of the `Lines`. Down past the newest line brings the draft back into the buffer and removes it from the ring. Enter removes it too.
- Up does nothing while the ring is empty. It also does nothing when the draft cannot be kept: a line longer than 255 characters, longer than the ring, or a draft that would drop the only older entry. With `Lines` at 2 and a ring that holds one entry, the entry stays and Up does nothing.
- When the ring is full, the oldest entries leave to make room. A draft never takes the last older entry; Up does nothing instead.
- A line that holds a `Secret` (HiddenInput) is not kept, and Up does nothing while the line being typed holds one. See CommandHiddenInput.md.
- Ctrl+C and a terminal change end the line, so the draft leaves the ring with them. The next Up starts again from the newest entry.

`Lines` must be at least 2, so that one entry is left to recall while the draft takes another.

---

## Screen

A recall writes the line from the start: `CSI n D` back over the cursor, the new text, and `CSI K` to clear the rest. The parser then replays the new text.

---

## Structure

| Part | Use |
|---|---|
| `struct History` | The module. `Needs` is `CommandLineBufferFeature`. Takes `Up` and `Down` in `OnKey`. Stores the line in `OnLineEnd` |
| `ring`, `first`, `used`, `count` | The byte ring, the offset of the oldest byte, the bytes in use, and the entries |
| `shown` | 0 while the line being typed is shown, otherwise the entry (counted back from the newest) that is shown. The draft is entry 0 during a recall |
| `EntryStart(back)` | The offset of an entry, from the newest, by walking the lengths backwards |
| `Push(text, keepOlder)`, `OlderSurvive`, `DropOldest`, `DropNewest` | Keep and remove entries. `keepOlder` refuses a draft that would drop the last older entry |

---

## Limits

- Host size: 160 bytes for `History<4, 128>`, which is the ring plus the counters. Not measured on the G431.
- A recall copies the entry through a 255-byte stack buffer.
- The ring and its counters are the object's. The module uses no heap.
