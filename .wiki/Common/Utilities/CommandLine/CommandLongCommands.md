# CommandLongCommands

`Common/Utilities/CommandLine/CommandLongCommands.h`: the `LongCommands` module. A command runs over several `Process` passes: its handler returns `CommandContinue` to run again on the next pass, and a `ResultStatus` to finish. Listed as `CommandLine<Set, App, Help, LongCommands>`.

---

## Handler

```cpp
struct TypeState {
	uint32 sent = 0;
};

CommandStep Type(CommandLongCall<AppContext, TypeState> call, uint8 count) {
	if (call.cancelled) {
		return ResultStatus::ok;
	}

	if (call.state.sent == count) {
		return ResultStatus::ok;
	}

	call.out.Write("*");
	call.state.sent++;
	return CommandContinue;
}
```

- The first parameter is a `CommandLongCall<Context, State>`: `out`, `app`, `state` and `cancelled`. `Context` is the type the handler declares, as for `CommandCall`. The rest are the argument values, as for any command.
- The handler's return type is `CommandStep`. A `ResultStatus` converts to a final step, so a handler can return one. `CommandContinue` is the other step.
- `state` is made before the first call, and it is kept until the command ends. A second run starts with a new state. `State` must be trivially destructible: a compile error otherwise (`a long command's state must be trivially destructible`).
- Output in chunks is the handler's job: each call writes what it has.

---

## Rules

- **The first step runs at Enter.** The line stays open while the command continues. The prompt comes when the command ends.
- **One step per `Process` pass.** The console runs the next step on each pass until the handler returns a final status. The pass that ends the command then reads the input that waited.
- **Input waits.** Bytes typed while the command runs are not echoed and not parsed. They stay in the input ring. The ring holds 32 bytes, or the size of a listed `InputRing<N>`. Input past that size is lost, and `InputOverflow` is raised before the next step.
- **Ctrl+C cancels.** The handler gets one call with `cancelled` set, and the step it returns is ignored. The bytes typed before the Ctrl+C are dropped with the command. The bytes after it are read as usual. The `Interrupt` event follows: `^C` and a new prompt, or the set's `Default()`.
- **A failed command** ends with a status other than `ok`. It raises `CommandFailed` with the command's name, as an ordinary command does.
- **Closing the terminal** cancels a running command, with the cancel call. No `Interrupt` event is raised; the `TerminalClosed` event follows.
- **Listing the module is required.** A set with a long command, without `LongCommands` listed, is a compile error: `a long command needs the LongCommands module, listed in the console`.

---

## Storage

The console keeps one state at a time, in a slot sized from the largest state of the set and its groups, aligned to the largest alignment. A set with no long command keeps a one-byte placeholder, since an array cannot be empty. With `LongCommands` listed, the console also keeps the run's name (a `string_view`, 16 bytes on the host) and a flag, so a listed module with no long command still costs those bytes. Each command's parser state and values are kept by the parser until the command ends, and they are not copied.

---

## Structure

| Part | Use |
|---|---|
| `struct LongCommands` | The module. `Provides` is `CommandLongFeature`. It has no hooks |
| `CommandLongCall<Context, State>` | The first parameter of a long handler (CommandTypes.h) |
| `CommandStep`, `CommandContinue` | The return type of a long handler, and the step that continues it (CommandTypes.h) |
| `CommandStepMode` | `Start`, `Continue` and `Cancel`: the calls the console makes (CommandTypes.h) |
| `CommandSet::Step` | Runs one step of a command (CommandSet.h) |

`LongCommands` is a feature rather than a hook: the run needs the console's ring and parser, and a slot sized by the set. The code is in the console, behind the feature. Without the module listed, it is not built.

---

## Limits

- One long command runs at a time. A second command cannot start until the first ends.
- A long command cannot read input while it runs; it can only be cancelled by Ctrl+C.
- The state is not destroyed, so it must hold no resources.

See `CommandLine.md` for the console's `Process`, and `CommandSetBuilder.md` for the handler forms.
