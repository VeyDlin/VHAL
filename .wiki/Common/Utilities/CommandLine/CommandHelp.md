# CommandHelp

`Common/Utilities/CommandLine/CommandHelp.h`: the `Help` module. It answers `help`, `help <path>` and `<path> -h` from the set's declarations. Listed after the application type: `CommandLine<Set, App, Help>`. A trailing `-h` is a request after `help` too: `help motor -h` answers as `motor -h`, and `help -h` lists the top level.

For the output, the rules and the limits, see README.md, Help.

---

## Structure

| Part | Use |
|---|---|
| `struct Help` | The module. Hooks `OnCharacter`, `OnEvent` and `OnLineEnd` (returns true for a help request). Has no `Provides` or `Needs`. `KeepsLineCopy` is true, so the order check finds it |
| `CommandHelpWord(text, position)` | The next word of a text, or an empty view |
| `CommandHelpPad(out, n)` | `n` spaces |
| `CommandHelpDescribe(out, declaration)` | An argument's kind, range or values, and default. A float writes its range and default with two decimals. A secret writes `secret text of at most N characters, shown as *`, with no default |
| `CommandHelpUsage(out, declaration)` | `<name>` or `[name]` |
| `CommandHelpCommandPage` | The item visitor: writes one command, from its typed `CommandItem` |
| `CommandHelpCommandVisit` | The level visitor for a command, through `VisitCommand` |
| `CommandHelpLevelPage` | The level visitor: writes a group's header and its entries |
| `CommandHelpLookup` | The level visitor: finds a word by `TreeStep` and `TreePayload` |

The visitors are the set's typed view: `VisitLevel` gives the typed level at a group path, and `VisitCommand` gives the typed item of a command. A command's arguments are read with `CommandPackForEach`, so the output uses each declaration's real type. The `uint64` limits are printed in full, not clamped.

---

## Hooks

- `OnCharacter` copies the characters as the console reads them. A tab is a space. Backspace and DEL remove the last one. Other bytes outside `0x20`–`0x7E` are not copied, as the console ignores them. It returns false, so the console still reads the character.
- `OnEvent` clears the copy for `Interrupt`, `TerminalOpened` and `TerminalClosed`, and marks it lost for `InputOverflow`. It returns false.
- `OnLineEnd` answers the line when it is not lost, then clears the copy. The line is the `LineEditing` buffer when that is listed (`WithModule<CommandLineBufferFeature>`), with no length limit. Otherwise it is the copy, and a copy that overflowed (`tooLong`) is not answered. It returns true when it answered. Its `static_assert` requires a set with commands or groups.
- `tooLong` marks a copy that overflowed, which is kept apart from `lost` (`InputOverflow`), so a long line with `LineEditing` is still answered.

---

## Answer

`Answer<Commands>(out)` splits the copy into words, and counts them and keeps the last one.

- The first word is `help`, and no command of the top level is named `help`: the path is the words after the first.
- Otherwise, when the last word is `-h` and there are at least two words: the path is the words before the last one.
- Otherwise it is not a request, and the line runs.

It then walks the path. Each word is looked up at the current level with `VisitLevel`, from `groups` and `depth`. A word that names nothing prints `unknown command '<word>', try help`. A group pushes its index on `groups` and its name on `names`. A command ends the walk.

Then it prints the command at its level with `VisitCommand`, or the level with `CommandHelpLevelPage` (the header only when the path ends at a group).

`groups` and `names` have `Commands::GroupDepth + 1` entries, so a set with no groups still has one.

---

## Limits

- `LineSize` is 64. A longer line is not answered.
- The copy and its state are the object's: 64 bytes and a length, in the console object when `Help` is listed.
- The module is not thread safe. It is used from `Process` only, as all the module hooks are.
- A module listed before `Help` that takes a character, a key or an event hides it, so the copy can differ from the console's line. That is a compile error, except for the modules on the line buffer (`LineEditing`, `History`), which Help does not need the copy for.
