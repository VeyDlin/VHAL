# CommandLevel

`Common/Utilities/CommandLine/CommandLevel.h`: the tables of one level of a command set, built at compile time.

---

## Tables

| Table | Holds |
|---|---|
| `nodes` | The name tree of the commands and groups, then one tree per enum argument |
| `entries` | Commands and groups in declaration order: name, help, `index` (among commands or among groups) and `isGroup` |
| `commands` | Per command: first argument, argument count, value bytes, entry |
| `arguments` | Per argument: `CommandArgumentInfo` (see `CommandArgument.h`) |

The element types are `CommandEntryInfo` (`name`, `help`, `index`, `isGroup`), `CommandRecord` (`firstArgument`, `argumentCount`, `valueBytes`, `entry`) and the tree node `CommandTreeNode` (CommandNameTree.h). `CommandLevelCounts` holds the four sizes that `CommandMeasure` gives.

`CommandLevel<Nodes, Arguments, Entries, Commands>` has arrays of exactly those sizes. `CommandMeasure(builder)` gives the sizes, and `CommandBuildLevel<...>(builder)` builds the exact-size level. The build runs in scratch arrays of `CommandMaxNodes`, `CommandMaxArguments` and `CommandMaxEntries`, then copies the used part, so flash holds no unused entries.

A level with no commands and no groups has an empty tree.

---

## Layout

Each command's values sit in its value area in declaration order. Each value starts at its alignment, and the command's `valueBytes` is the end of the last one. For `mode` with an enum and a `String<8>`, the enum is at 0 and the string at 8, so `valueBytes` is 24.

---

## Limits

`CommandLimits` gives the maxima over a set and every group under it: arguments, value bytes and alignment, slot bytes and alignment, group depth, and the longest name (`nameLength`, which sizes the parser's name rows). A group contributes its own limits with one more level of depth, so a `CommandSet` sees its whole tree. `CommandLimitsOf(level, builder)` combines them.

---

## Build order

Items are visited in declaration order. For each command, its name goes into the level tree, and then each argument: its layout offset, its slot size, and for an enum a new tree of its value names. Duplicate names fail at this point with `duplicate names at one level` or `duplicate enum names`.
