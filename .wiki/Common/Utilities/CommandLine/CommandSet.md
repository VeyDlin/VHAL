# CommandSet

`Common/Utilities/CommandLine/CommandSet.h`: a command set built at compile time from a builder chain. It holds the tables of its level in flash and gives the console the operations it needs.

---

## Declaring a set

```cpp
using AppCommands = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("key", "press a key", &Key, Argument<uint8>("code"))
		.Default(&OnEvent);
}>;
```

`Make` is a captureless lambda that returns the builder. `CommandSet` takes it as a function-pointer template argument, so the builder chain is a constant expression.

---

## Constants

| Name | Meaning |
|---|---|
| `CommandCount`, `EntryCount`, `GroupCount` | Commands, commands and groups together, and groups of this level |
| `HasDefault`, `HasInterrupt` | Whether the chain has a `Default()` or an `OnInterrupt()` |
| `Root` | Node index of the root of this level's name tree (always 0) |
| `GroupDepth` | Deepest group nesting under this set. The group path of the console needs this many indexes |
| `MaxArguments` | Largest argument count of any command in this set or in any group under it. Sizes the parser's argument rows |
| `MaxNameLength` | Longest command or group name in this set or in any group under it. Sizes the parser's name rows |
| `MaxValueBytes`, `MaxValueAlign` | Size and alignment of the value area. Allocate `alignas(MaxValueAlign) std::byte values[MaxValueBytes]` |
| `MaxSlotBytes`, `MaxSlotAlign` | Size and alignment of the storage a reader is placed in. `MaxSlotBytes` is rounded up to `MaxSlotAlign`, so each row of readers starts aligned on every target |

The tables are `static constexpr` and sized exactly. A set with only a `Default()` has no tree, no entries and no arguments, so it has no tables at all.

---

## Operations for the console

| Operation | Use |
|---|---|
| `TreeStep(node, c)` | The node that `c` leads to from `node`, or `CommandNoNode`. Walks names one character at a time |
| `TreePayload(node)` | The entry of a complete name (its index in `EntryInfo`), or the value index of a complete enum value. `CommandNoNode` for a prefix |
| `TreeFirstChild(node)`, `TreeSibling(node)`, `TreeCharacter(node)` | The children of a node, and the character it holds. Children are in declaration order: the sibling list is built by appending, so the first declared name is the first child |
| `EntryInfo(entry)` | Name, help, `isGroup` and `index` of an entry |
| `CommandName(command)` | The name of a command |
| `ArgumentCountOf(command)` | Number of arguments of a command |
| `ArgumentInfoOf(command, position)` | Metadata of one argument: name, kind, range, `hasDefault`, value offset, enum root, slot size |
| `CreateSlot(command, position, storage)` | Placement-creates the reader of an argument in `storage` and returns it. `nullptr` for a position past the end |
| `EnumNode(command, position, slot, node)` | The node an enum reader stands at, for Completion. The reader is read as the type its declaration gives, so the slot base needs no virtual member for it. Returns `false` when the text is refused. Only Completion calls it |
| `StoreDefault(command, position, values)` | Writes the default of an optional argument into the value area. Does nothing for a required one |
| `Invoke(command, app, out, values)` | Calls the command's handler with the values read from the value area, and returns its `ResultStatus`. `notFound` for an unknown index |
| `Step(command, app, out, values, state, mode)` | Runs one step of a command: `Start` makes a long command's state in `state` and runs its first step, `Continue` runs the next, `Cancel` makes the one cancel call. An ordinary command returns its status as a final step (LongCommands) |
| `VisitArgument(command, position, visitor)` | Calls the visitor with the typed declaration of an argument. A float range is read from it |
| `HasLong`, `HasFloat`, `HasSecret` | Whether the set has a long command, a float argument, or a secret argument. Each needs its module listed |
| `MaxStepBytes`, `MaxStepAlign` | The largest long command's state, and its alignment, over the set and its groups |
| `CallDefault(app, out, event)` | Calls the default handler. Does nothing when there is none |
| `CallInterrupt(app)` | Calls the interrupt handler. Does nothing when there is none |
| `CheckContext<App>()` | Compile-time check that `App&` converts to every handler's context |
| `VisitLevel(groups, depth, visitor)` | Calls `visitor.template operator()<Set>()` with the typed set at the end of a group path |
| `VisitCommand(command, visitor)` | Calls `visitor(item)` with the typed `CommandItem` of a command of this level, so a caller can read its declared arguments. Help uses it |

`VisitLevel` is how the console reaches a group. A group is a `CommandSet` of its own, with its own tree, arguments and value area. The console keeps the group path as `depth` indexes, and passes a visitor template. The visitor's `operator()` is called once with the set type at the end of the path, and it calls that set's operations. An empty path (`depth == 0`) calls the visitor with this set.

```cpp
struct Walker {
	template<typename LevelSet>
	void operator()() {
		LevelSet::Invoke(0, app, out, values);
	}
};

uint16 path[1] = {0};
Walker walker;
AppCommands::VisitLevel(path, 1, walker);
```

Each level has its own name tree, so a word is walked in the tree of the level the path names. A group word is looked up in its parent level, and the console then moves to the group's level and walks its tree.

---

## Value area and slots

The console keeps one value area, sized by `MaxValueBytes` for the largest command of the whole set, and one slot row per argument position, each `MaxSlotBytes`. A command's values sit in the value area at the offsets `ArgumentInfoOf` gives. A reader writes its value there in `Finish`, and `Invoke` reads them back with `memcpy`, so the values need no alignment from the caller.

The console creates a reader for an argument when its text starts. The parser keeps one storage row per argument position, sized `MaxSlotBytes`, and the readers of the arguments typed so far stay alive, because Backspace goes back into an earlier argument. Its name rows hold `MaxNameLength` nodes for each group level.

---

## Limits

- Names are at most 32 characters for enum values. Command and group names have no length limit beyond the tree.
- The builder holds at most 256 arguments, 256 entries and 1024 tree nodes in one level, while building. Flash holds only the exact sizes.
- A level holds at most 256 commands and groups and 256 arguments (`CommandMaxEntries` and `CommandMaxArguments`, the builder's bounds). Group indexes are `uint16`.
