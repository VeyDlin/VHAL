# CommandSetBuilder

`Common/Utilities/CommandLine/CommandSetBuilder.h`: the chain of calls that declares a command set. Each call returns a new builder type that holds one more item, so each handler keeps its own function type and nothing is stored through a cast.

---

## Items

| Item | Holds |
|---|---|
| `CommandItem<Handler, Args...>` | Name, help, the handler, and the arguments in a `CommandPack` |
| `GroupItem<Set>` | Name and help of a group. The group's `CommandSet` is the type |
| `DefaultItem<Handler>` | The default handler |
| `InterruptItem<Handler>` | The interrupt handler |

---

## Handler shapes

| Handler | Shape |
|---|---|
| Command | `ResultStatus f(CommandCall<Ctx>, value types...)` |
| Long command (LongCommands) | `CommandStep f(CommandLongCall<Ctx, State>, value types...)` |
| Default | `void f(CommandCall<Ctx>, const CommandEvent&)` |
| Interrupt | `void f(Ctx&)` |

`CommandHandlerOf<Handler>::Long` tells the two command shapes apart, and `::State` is the long form's state type. A long command is a `CommandItem` like any other, and its state is in the console's step slot, sized by the set's limits.

`CommandHandlerOf<Handler>` reads the shape of a command handler: its context `Ctx`, and its parameter types as a `std::tuple` type list. The tuple is only a type list, never a value, so it is safe in a constant expression.

---

## Checks

- Each name is checked by `CommandRequireName` (`CommandNameTree.h`).
- A command needs help text, and so does a group.
- The optional arguments come after the required ones, and no two arguments of a command have the same name.
- Each argument's own `Verify()` runs: a default inside the range, and an enum default among the values.
- The handler's parameter types must equal the arguments' value types, in order.
- `Default()` and `OnInterrupt()` may each appear once.
- A group has at least one command or group, which is `Set::EntryCount > 0`.

A group has no `Default()` or `OnInterrupt()` of its own: the builder refuses one with `a group has no Default() or OnInterrupt(), the top set handles those`. The console calls only the top set's handlers, so a group's would never run.

Duplicate names at one level are found when the level is built (see `CommandLevel.h`), because they need the whole chain.

---

## Example

```cpp
CommandSetBuilder()
	.Command("move", "move the pointer", &Move, Argument<int8>("x").Range(-127, 127), Argument<int8>("y").Range(-127, 127), Argument<bool>("fast").Default(false))
	.Group<PidCommands>("pid", "pid tuning")
	.OnInterrupt(&StopMotor)
	.Default(&OnEvent);
```
