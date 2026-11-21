# CommandMessages

`Common/Utilities/CommandLine/CommandMessages.h`: the standard default handler. It prints the texts for events when the set has no `Default()` of its own.

---

## Functions

| Function | Prints |
|---|---|
| `CommandWriteEvent(out, event)` | One event's text, with its line end. Nothing for `EmptyLine`, `Word`, `EndOfLine`, `TerminalClosed` and `Command`: the console prints the prompt after each line |
| `CommandWriteProblem(out, event)` | The reason an argument is refused, after its name |
| `CommandWriteRange(out, event)` | `minimum..maximum` |

`CommandWriteEvent` prints:

| Event | Text |
|---|---|
| `CommandFailed` | `<command>: <status name>` |
| `UnknownCommand` | `unknown command '<text>', try help` |
| `ArgumentError` | `<command>: ` and the problem |
| `InputOverflow` | `input lost` |
| `Interrupt` | `^C` |
| `TerminalOpened` | `VHAL command line` |

The problem texts, by fault: `<arg> is missing`, `too many arguments`, `missing closing quote`, `<arg> must be <min>..<max>`, `<arg> must be at most <n> characters`. Otherwise by the argument's kind: `must be a number`, `must be on, off, true, false, 1 or 0`, `must be a listed value`, or `is not valid`.

---

## Where it is used

`CommandLine::Raise` calls `CommandWriteEvent` only when the set has no `Default()` and no module took the event. So a build that has its own `Default()` does not link this file's status text (see CommandStatusText.md). A set with commands and no `Default()` links it.

The argument's name in an error is the one declared. The command's name is the name it is typed as, not the group path.
