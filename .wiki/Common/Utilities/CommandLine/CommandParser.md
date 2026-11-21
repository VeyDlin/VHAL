# CommandParser

`Common/Utilities/CommandLine/CommandParser.h`: the streaming parser of a console. It reads one character at a time, keeps the state of the token being typed, and at the end of the line says whether a command runs and with which values. It keeps no line text.

---

## Sizes

| Constant | Value |
|---|---|
| `Levels` | `GroupDepth + 1`, the group levels of the set and its root |
| `NameDepth` | The longest name, at least 1 (`CommandArraySize`) |
| `Arguments` | The most arguments of any command, at least 1 |
| `SlotBytes`, `ValueBytes` | The largest reader and value area, at least 1 |
| `CommandWordSize` | 16: the characters a word keeps for messages and `Word` events |

---

## State

Per group level: the group path (`groups`), the typed and matched characters of the name (`nameTyped`, `nameMatched`), the spaces before the name (`nameGap`), the tree node after each matched character (`nameNodes`), and the word's first characters (`words`).

Per argument position: the typed count (`argTyped`), the spaces before it (`argGap`), its reader (`slots`), the reader's storage (`slotStorage`, one row per position, so Backspace can go back into an earlier argument), and the value area (`values`).

Scalars: `level`, `tail` (characters refused after a stop), `spaces`, `command`, `argument`, `phase` (`Name`, `Argument` or `Gap`), `after` (`Start`, `Group`, `Command` or `Argument`: what the gap follows), `stop` (`None`, `Unknown` or `Extra`), and the word-mode fields `wordLength`, `wordGap`, `committed`.

---

## Operations

| Function | Use |
|---|---|
| `Reset()` | Forgets the line. The next character starts a new one |
| `Push(char)` | One character. Word mode when the set has no commands, else command mode |
| `Backspace()` | Removes the last character. False when nothing was there |
| `Evaluate(event)` | Command mode, at Enter. Returns a `CommandEnd` and fills `event` for an error |
| `WithLevel(f)` | Calls `f` with the typed set at the current group path |
| `Command()`, `Values()` | The command of a `Run` line, and its value area |
| `WordEnds(c)`, `HasWord()`, `WordLength()`, `WordText()` | Word mode: whether a character ends the current word, and the word |

### CommandEnd

`Empty`: a line with no words (`EmptyLine`). `Unknown`: an unknown word (`UnknownCommand`). `Fault`: an argument error (`ArgumentError`). `Run`: call the command with `Values()`.

---

## Command mode

`PushCommandIn<L>` reads a character at the current place:

- After a stop, a character only counts (`tail`).
- A space in a name closes the name (`CloseName`). A command takes its arguments, and a group opens its level. A name that matches no entry stops the parse with `Unknown`.
- A space in an argument ends it, unless the reader is quoting (`Quoting()`). A quoted space goes to the reader.
- A character in a name walks the tree (`PushNameCharacter`). One that matches nothing is counted and not matched.
- A character in a gap starts a name (after the start or a group) or an argument (`StartArgument`). An argument past the last one stops the parse with `Extra`. Otherwise the argument's reader is created in its storage, and gets the character.

`Backspace` (`BackspaceCommand`) undoes the same steps in reverse: a stopped line first gives back its counted characters, then a name steps back in the tree, an argument calls the reader's `Pop`, and a gap gives back its spaces and reopens the token before it.

### Evaluate

`EvaluateIn<L>` at Enter:
- a stop of `Unknown` is `UnknownCommand`, with the word of the level it stopped in;
- a stop of `Extra` is `ArgumentError` with fault `Extra`, and the command's name;
- a name that is not complete, or names a group, is `UnknownCommand`;
- an empty line is `Empty`;
- a group name alone, or a gap after a group, is `UnknownCommand` with the group's name;
- otherwise `RunCommand` reads every argument: each started one is finished by its reader, and each one not started takes its default (`StoreDefault`) or is `Missing`. The first refusal is the `ArgumentError`, and the event names the argument.

---

## Word mode

For a set with no commands, a character that is not a space ends the current word when it comes after one (`WordEnds`). The word is then final, and its `Word` event is raised before the character is read (in `CommandLine`). The word keeps its first 16 characters.

`BackspaceWord` erases the spaces after the word first, then the word. Once a later word has started, the earlier one is committed: one space before the current word stays, so the line never shows two words glued together. Under this rule, after `a b` two Backspaces leave `a `, and a third does nothing.

---

## Limits

- A name has at most `NameDepth` characters, which is the longest name of the set.
- A refused character is only counted, so a long line that stops early costs no extra state.
- Group indexes are `uint16`, as in `CommandSet`.
