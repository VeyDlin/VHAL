# Command Line

`Common/Utilities/CommandLine/`: a text console for any byte stream (UART, USB CDC-ACM, or anything with a `Print`). Commands are declared once, with typed arguments, and the compiler checks the declaration. The console parses input as it arrives, so no line is kept. No heap is used, and no exception, RTTI or `std::function`.

Status: the declaration side, the console, and all modules are done: `Help`, the editing modules (`LineEditing`, `History`, `Completion`), `LongCommands`, `HiddenInput`, `Ansi` and `FloatArguments`. The modules are measured on the Cortex-M4 (Memory), but not on the G431 board, and their tests are host tests only.

---

## Example

A complete program: a motor command set in its own header, the application, the console and the receive interrupt. The file compiles and runs on the host with the sim's `Print` shim; on a board, `StdoutPrint` is the board's `Print`.

```cpp
#include <Utilities/CommandLine/CommandHelp.h>
#include <Utilities/CommandLine/CommandLine.h>
#include <Utilities/CommandLine/CommandSet.h>
#include <Utilities/Console/Print.h>
#include <cstdio>


// Motor control: its own command set, which would live in its own header.
struct MotorControl {
	int16 rpm = 0;
	int stops = 0;
};


ResultStatus SetSpeed(CommandCall<MotorControl> call, int16 rpm) {
	call.app.rpm = rpm;
	return ResultStatus::ok;
}


// Ctrl+C runs this inside Feed, so it does only what is safe in an interrupt.
void StopMotor(MotorControl& motor) {
	motor.rpm = 0;
	motor.stops++;
}


using MotorCommands = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("speed", "set the speed", &SetSpeed, Argument<int16>("rpm").Range(-1000, 1000).Default(0));
}>;


// The application derives from every interface its command sets use.
struct App : MotorControl {
	uint8 keyCode = 0;
	int events = 0;
};


ResultStatus PressKey(CommandCall<App> call, uint8 code) {
	call.app.keyCode = code;
	return ResultStatus::ok;
}


// Every event without its own handler comes here.
void OnEvent(CommandCall<App> call, const CommandEvent& event) {
	call.app.events++;

	if (event.kind == CommandEventKind::ArgumentError) {
		call.out.Write("bad argument: ");
		call.out.WriteLine(event.argument);
	}
}


using AppCommands = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("key", "press a key", &PressKey, Argument<uint8>("code").Range(0, 255))
		.Group<MotorCommands>("motor", "motor control")
		.OnInterrupt(&StopMotor)
		.Default(&OnEvent);
}>;


// A Print that writes to the host's stdout. On a board, use the board's Print.
class StdoutPrint : public Print {
public:
	void WriteRaw(const char* data, size_t size) override {
		std::fwrite(data, 1, size, stdout);
	}
};


App app;
StdoutPrint out;
CommandLine<AppCommands, App, Help> console(app, out);


// The receive interrupt. Here the host sends the bytes from main.
void OnUartByte(uint8 byte) {
	console.Feed(byte);
}


int main() {
	const char* input = "motor speed 300\rmotor speed 5000\rkey 7\rmotor -h\rhelp\r\x03motor stop\r";

	console.SetTerminalOpen(true);

	for (const char* character = input; *character != 0; character++) {
		OnUartByte(static_cast<uint8>(*character));
		console.Process();
	}

	// Events: TerminalOpened, the bad speed, the Interrupt and the unknown stop. Help lines raise none.
	return app.keyCode == 7 && app.stops == 1 && app.rpm == 0 && app.events == 4 ? 0 : 1;
}
```

Its output, with `Help` listed (`\r\n` shown as line breaks, the echo as typed):

```
> motor speed 300
> motor speed 5000
bad argument: rpm
> key 7
> motor -h
motor: motor control
  speed  set the speed
> help
  key    press a key
  motor  (group) motor control
> > motor stop
>
```

The Ctrl+C after the listing prints only the prompt, because this set's `Default()` prints nothing for an `Interrupt` event. The standard handler prints `^C`. The `motor stop` line raises an `UnknownCommand` event, which the same `Default()` also takes silently.

---

## Declaring commands

The chain is a captureless lambda, as for the FAT12 image and the USB descriptors. `CommandSet` evaluates it at compile time, and each rule that the chain breaks is a compile error with its message.

| Call | Meaning |
|---|---|
| `.Command(name, help, handler, args...)` | A command. The handler is `ResultStatus f(CommandCall<Ctx>, values...)`, with one parameter per argument, of the argument's value type. |
| `.Group<Set>(name, help)` | A command set from another header, nested under a name. The set is a type, so it is passed as a template argument. |
| `.OnInterrupt(&fn)` | Ctrl+C handler, `void fn(Ctx&)`. Runs at once, inside `Feed`, see Interrupt below. |
| `.Default(&fn)` | Handler of every event without its own handler, `void fn(CommandCall<Ctx>, const CommandEvent&)`. |

`CommandSetBuilder().Default(&OnEvent)` alone is the smallest set. It has no tree and no table, so it costs flash only for its handler.

### Handler forms

- A command handler takes `CommandCall<Ctx>` first. `call.out` is the console's `Print&`, and `call.app` is a `Ctx&`. The other parameters are the value types of the arguments, in order. The builder checks them against the declared arguments.
- `Ctx` is the type the handler's own level declares. A command set in another header declares its own interface, so it is not bound to one application type.
- The default handler is `void fn(CommandCall<Ctx>, const CommandEvent& event)`.
- The interrupt handler is `void fn(Ctx& app)`. It has no `Print`, because it runs in `Feed`.
- Each handler's `Ctx` must be the application type or one of its bases. `CommandSet::CheckContext<App>()` checks this at compile time, and the console calls it when it is bound to an application type. Two consoles with different application types can share a set.

---

## Arguments

| Declaration | Value type | Notes |
|---|---|---|
| `Argument<int8>("x").Range(-127, 127).Default(0)` | `int8` to `uint32`, `int64`, `uint64` | Range and Default are checked against the type |
| `Argument<bool>("on").Default(false)` | `bool` | Words: `on`, `off`, `true`, `false`, `1`, `0`. Lowercase only |
| `Argument<Mode>("mode").Values({{"fast", Mode::Fast}, {"slow", Mode::Slow}})` | an enum | Names are matched through a tree |
| `Argument<String<16>>("name").Default("none")` | `String<16>` | The only text kept. Its capacity is the most characters as typed, quotes included |
| `Argument<T>("value")` | `T` | A custom type, through its parser (see `CommandArgument.h`) |

An argument with a `Default` is optional and must come after the required ones. Argument names, command names and enum value names use the characters `a-z`, `0-9`, `_` and `-`. An enum value name is at most 32 characters.

---

## Compile-time checks

Each of these is a compile error with the message in the second column.

| Rule | Message |
|---|---|
| Duplicate command, group or enum value name at one level | `duplicate names at one level`, `duplicate enum names` |
| Empty name | `a name is empty` |
| Name character outside `a-z`, `0-9`, `_`, `-` | `a name has a character outside a-z, 0-9, _ and -` |
| Empty help text | `a command has no help text`, `a group has no help text` |
| Required argument after an optional one | `a required argument comes after an optional one` |
| Range bound outside the type | `a range bound is outside the type of the argument` |
| Range with minimum above maximum | `a range has its minimum above its maximum` |
| Float range bound infinite or nan | `a float range bound is not finite` |
| Default outside the range | `the default is outside the range of the argument` |
| Default outside the type | `the default is outside the type of the argument` |
| Default not one of the enum values | `the default is not one of the enum values` |
| Enum value name longer than 32 characters | `an enum value name is longer than 32 characters` |
| String capacity of zero | `a string argument needs a capacity of at least 1` |
| String default longer than the capacity | `the default is longer than the string capacity` |
| Handler parameters not matching the arguments | `the handler parameters do not match the arguments` |
| Handler not `ResultStatus f(CommandCall<Ctx>, ...)` | `a command handler returns ResultStatus and takes CommandCall<Ctx> first` |
| Default handler shape | `a default handler is void(CommandCall<Ctx>, const CommandEvent&)` |
| Interrupt handler shape | `an interrupt handler is void(Ctx&)` |
| `Default()` or `OnInterrupt()` given twice | `Default() is given twice`, `OnInterrupt() is given twice` |
| Empty group | `an empty group has no commands or groups` |
| Two arguments of one command with the same name | `two arguments of a command have the same name` |
| Context of a handler not the application type or a base | `the context of a handler is not the application type or one of its bases` |
| A listed module needs a feature no listed module provides | `a module needs a feature that no listed module provides` |
| `Help` listed with a set that has no commands or groups | `Help needs a command or a group in the set` |
| A module that takes characters, keys or events listed before `Help` (`LineEditing` and `History` exempt) | `Help must be listed before a module that takes characters, keys or events` |

The rules are checked by `Common/Utilities/CommandLine/.tool/sim/BuilderErrors/check.sh`, one snippet per rule.

---

## Running a console

```cpp
CommandLine<AppCommands, App> console(app, out);            // out is a Print, app the application
CommandLine<AppCommands, App, InputRing<64>, Help> console(app, out);  // a ring of 64 bytes, and a listed module
CommandLine<AppCommands, App, Help, LineEditing<64>, History<4, 128>, Completion> console(app, out);  // the editing modules
```

| Call | Context it may run in | What it does |
|---|---|---|
| `Feed(byte)` | an interrupt, or the main loop | Calls the `OnInterrupt` handler when the byte is Ctrl+C. Stores the byte in the input ring. Never prints, and calls no other handler |
| `Process()` | the main loop or a task, never an interrupt | Drains the ring, one byte at a time, so the echo follows the input. Raises events, runs handlers and module hooks, and writes all output |
| `SetTerminalOpen(open)` | any context | Stores the state. The `TerminalOpened` or `TerminalClosed` event comes from the next `Process` |
| `Output()`, `Application()` | `Process` (from a module hook) | The console's `Print` and application, for modules |

- `Feed` is single-producer. Only one context may call it at a time, and `Process` is the only consumer. The ring is 32 bytes, or the size of a listed `InputRing<N>`, which must be a power of two.
- Bytes that do not fit the ring are counted and reported as `InputOverflow` at the next `Process`.
- Ctrl+C (`0x03`) is handled in `Feed`. An `OnInterrupt` handler runs at once, with no output. The byte is then queued like any other, so `Process` cancels the line where the byte was. A Ctrl+C that is lost because the ring is full still cancels the line, at its place: the bytes accepted before it are handled first, as usual.
- Memory: see Memory below.

### Input

- Printable characters are echoed as they arrive. Tab is a space, unless `Completion` takes it.
- Backspace (`0x08`) and DEL (`0x7F`) erase: the echo is `BS SPACE BS`, and only when something was erased.
- Line ends: CR, LF, CR LF and LF CR each end one line, and two CRs or two LFs in a row end two lines. Enter echoes `\r\n`.
- Escape sequences are decoded. Without a module that takes a key, arrow keys, Home, End and Delete inject nothing. A control byte inside a sequence breaks it and is read as input. The keys go to a module's `OnKey` hook first.
- Other control bytes and bytes above `0x7E` are ignored.

### Parsing, without a line buffer

Each character moves the state of its position. A command or group name walks the tree (one node per character, kept in the level's row). An argument keeps its reader in its own slot, with the number of characters typed. The gap after a token keeps the spaces typed after it, and what it follows, so Backspace can go back through any of them:

- Backspace in a name steps back in the tree. Backspace in an argument calls the reader's `Pop`.
- A space after a name or argument opens a gap. Backspace in a gap removes its spaces, then reopens the token before it, at its end.
- An unknown name, or an argument past the last one, stops the parse. The rest of the line is counted, and Backspace removes that count first.
- Several spaces separate tokens, and a space inside a quoted string does not.

Integers go through `IntegerParser` (decimal, `0x`, `0b`, a sign for a signed type). The value is checked against the declared range at Enter: `key 256` is `key: code must be 0..255`, and `key 9x` is `key: code must be a number`. Backspace can remove any digit, so a value typed out of range can be corrected before Enter.

Strings: a text that starts with a double quote is quoted. Inside it, `\"` and `\\` are escapes, and any other `\x` is kept as typed. A space is part of the text, and the closing quote ends it. Anything after the closing quote is invalid. A quote left open at Enter is `missing closing quote`. A text that does not start with a quote is the value as typed.

### Events

Every event goes to a module that takes it (`OnEvent`), then to the set's `Default()`, or to the standard handler when the set has none. The console prints the prompt after each line and after the greeting, whatever a handler printed.

| Event | When | Standard text |
|---|---|---|
| `CommandFailed` | a handler returned a status other than `ok` | `key: invalidParameter` (the status name) |
| `UnknownCommand` | a name matches no command, or a group name is given alone | `unknown command 'xyz', try help` |
| `ArgumentError` | a missing argument, an extra one, a value that is not a number, out of range, a string too long or not closed | `key: code must be 0..255`, `key: code is missing`, `key: too many arguments`, `key: code must be a number`, `key: code must be on, off, true, false, 1 or 0`, `key: mode must be a listed value`, `key: label must be at most 8 characters`, `key: missing closing quote` |
| `EmptyLine` | Enter on an empty line or a line of spaces | (prompt only) |
| `Word` | the default-only build: a word is final, when the next word starts or at Enter; with `LineEditing`, the words of the edited line are raised at Enter | (nothing) |
| `EndOfLine` | the default-only build: at Enter, after the words | (nothing) |
| `Interrupt` | Ctrl+C | `^C`, then the prompt |
| `TerminalOpened` | `SetTerminalOpen(true)` | `VHAL command line`, then the prompt |
| `TerminalClosed` | `SetTerminalOpen(false)`: the line is dropped | (nothing) |
| `InputOverflow` | bytes were lost to a full ring | `input lost` |

A `CommandEvent` has `kind`, `text` (the word of `UnknownCommand` and `Word`), `status` (of a failed command), `command` (the command an error is about), `argument` (the argument's declared name), `argumentKind`, `fault` (why it was refused), its range in `range` (`range.integers`, with `minimum` and `maximum`, and `unsignedMaximum` with `maximumUnsigned` set for a 64-bit unsigned maximum above the int64 range; `range.floats` for a float argument, with `minimum` and `maximum` as doubles, which shares storage with `range.integers`, so the event does not grow for a set without floats), and `truncated` on a `Word` event whose word is longer than the word buffer. A string that is not about an argument leaves the argument fields empty. The command is named by its own name, not by the group path: `gain: value must be ...`, not `motor pid gain: ...`.

With a `Default()`, the set's handler decides what each event prints or does, and the standard texts are not used.

### Default-only build

A set with no commands (`CommandSetBuilder().Default(&OnEvent)`, or none at all) has no tree. It splits the input into words and raises:

- `Word` when a word is final: when the next word starts, or at Enter for the last one. The text is the word, cut to its first 16 characters, and `truncated` is set when the word was longer.
- `EndOfLine` at Enter, after the words, or `EmptyLine` for a line with no word.

Backspace removes characters from the last word. While only spaces follow a word, it erases the spaces and then the word. Once a later word has started, the earlier word is final, and the space that keeps the two apart stays. So after `a b`, two Backspaces leave `a `, and a third does nothing: `a` cannot be erased from that position. This is the rule without `LineEditing`: a user who needs to edit a whole line lists `LineEditing`.

With `LineEditing`, no `Word` event is raised while the line is typed. At Enter the console raises the `Word` events of the edited line, from the buffer, then `EndOfLine`. Backspace goes back over the words too, since the buffer decides. So `ab cd`, Home, `x`, Enter gives `xab` and `cd`, and four Backspaces from `ab cd` leave `a`, which is the one `Word` raised at Enter. See CommandLineEditing.md.

### Interrupt

With `OnInterrupt`, Ctrl+C runs the handler in `Feed`, at once, from whatever context `Feed` runs in. The handler gets `App&` only, and must be short and safe in an interrupt. It is a convenience, not a safety function: an emergency stop that matters must not depend on the console.

Either way, `Process` cancels the line where the byte was, and an `Interrupt` event goes out. A cancelled line is not answered: a `help` typed before the Ctrl+C prints nothing.

A Ctrl+C that the full ring refuses is still taken at its place, as an accepted one is. Each refused press raises its own `Interrupt` when `Process` reaches the bytes accepted before it. Two refused presses at the same place give two `Interrupt`s. At most two refused presses wait at once; a third, while two wait, is dropped (see CommandLine.md).

---

## Modules

A module is a type listed after the application type. A module that is not listed costs nothing: none of its code is built.

```cpp
struct Echo {
	using Provides = EchoFeature;      // what the module provides, a type of its own. Optional
	using Needs = EditFeature;         // a feature a listed module must provide. Optional

	template<typename Console>
	bool OnCharacter(Console& console, char character);      // before the parser; true consumes the character
	template<typename Console>
	bool OnKey(Console& console, CommandKey key);            // a key of an escape sequence; true consumes it
	template<typename Console>
	bool OnLineEnd(Console& console);                        // at each line end, before its events; true takes the line
	template<typename Console>
	bool OnEvent(Console& console, const CommandEvent& event);  // before the default handler; true consumes the event
	template<typename Console>
	bool OnPrompt(Console& console);                         // before the prompt; true means the module wrote it
};
```

Each hook is optional. A `void OnLineEnd` is also accepted. The console passes itself, so a module writes with `console.Output()` and reaches the application with `console.Application()`. Every module gets each line end, so each can reset its own state. The other hooks stop at the first module that takes the character, key or event.

A module whose `Needs` no listed module provides is a compile error, `a module needs a feature that no listed module provides`.

`CommandModules.h` has the dispatch. `CommandLine.h` shows where each hook runs.

### Editing modules

| Module | What it adds | Cost | Needs | Page |
|---|---|---|---|---|
| `LineEditing<N>` | Cursor keys, Home, End, Delete, Ctrl+A, E, K, W; insert in the middle | an N-byte line buffer | | [CommandLineEditing](CommandLineEditing.md) |
| `History<Lines, Bytes>` | Up and Down recall lines, into the `LineEditing` buffer | a ring of Bytes | `LineEditing` | [CommandHistory](CommandHistory.md) |
| `Completion` | Tab completes a command, group or enum value from the name tree | code only | | [CommandCompletion](CommandCompletion.md) |

### Other modules

| Module | What it adds | Cost | Needs | Page |
|---|---|---|---|---|
| `LongCommands` | A handler returns `CommandContinue` and runs again on each `Process` pass; Ctrl+C cancels it | a step-state slot, the largest state of the set | | [CommandLongCommands](CommandLongCommands.md) |
| `HiddenInput` | `Secret<N>` arguments, echoed as `*` | one byte, and the echo | | [CommandHiddenInput](CommandHiddenInput.md) |
| `Ansi` | Coloured errors and prompt; Ctrl+L clears the screen | code only | | [CommandAnsi](CommandAnsi.md) |
| `FloatArguments` | `float` and `double` arguments, read by `FloatParser`, with `Range` and `Default` | the float parser (several kilobytes of flash) | | [CommandFloatArguments](CommandFloatArguments.md) |

`LongCommands`, `HiddenInput` and `FloatArguments` are features, not hooks: the console does the work, so the set's need for one is checked. A set with a long command, a `Secret` or a `float` needs its module listed, and a compile error says so. `Ansi` is a hook module, listed after `Help`.

The line buffer is `CommandLineBufferFeature`, provided by `LineEditing`. `Help` reads the line from it when it is listed, so an edited line is answered whole.

`Help` must be listed before every module that takes a character, a key or an event, except `LineEditing` and `History`, which work on the buffer. A `Completion` listed before `Help` is a compile error: `Help must be listed before a module that takes characters, keys or events (LineEditing and History are exempt)`.

The console methods that modules use are `Output()`, `Application()`, `Type(char)` (through the modules), `Append(char)` (the parser and the echo, no modules), `EraseLast()`, `Replay(text, size)`, `RedrawPrompt()`, `Changes()`, `Parsed()` (read only) and `WithModule<Feature>(visitor)`. See CommandLine.md.

---

## Help

`CommandHelp.h`: `CommandLine<Set, App, Help>`. It prints the set from its declarations: names, help texts, argument names and kinds, ranges, defaults, enum values and group contents.

| Line | Output |
|---|---|
| `help` | The top level: each name with its help text. A group is marked `(group)` |
| `help motor` | The group's name and help text, then its entries |
| `help motor pid gain` | The command's name and help text, `usage: motor pid gain <value>`, then one line per argument |
| `key -h` | The same as `help key` |
| `motor -h` | The same as `help motor` |

A command's argument line gives its kind and range, or its values, and its default: `number 0..255`, `number -1000..1000, default 0`, `on or off, default off`, `one of fast, slow`, `text of at most 8 characters, default "none"`, or `value` for a custom type. `<name>` is required and `[name]` optional.

Rules:
- The word `help` is a request only when no command of the set is named `help`.
- A path is read word by word, and ends at its first command. Words after that command are ignored.
- A word that names nothing at its level prints `unknown command 'xyz', try help`.
- A request takes the line: no events are raised and no command runs. A `key -h` with a missing argument still prints the help.
- A trailing `-h` is always a request, even where it would be a valid string argument.

Without `LineEditing`, Help keeps its own copy of the line, up to 64 characters (`LineSize`), because the console keeps none. Backspace removes from the copy as it does from the console. A line longer than that is not a request, and it runs as a normal line. Lost input (`InputOverflow`), Ctrl+C and a terminal change drop the copy. With `LineEditing` listed, Help reads the line from its buffer, and the copy is not used.

Limits:
- Help must be listed before any module that takes characters, keys or events, except `LineEditing` and `History`. This is a compile error otherwise (see Modules).
- A set with no commands and no groups has nothing to describe: a listed `Help` is a compile error.

---

## Memory

Measured on the Cortex-M4 with Arm GNU Toolchain 15.2.1, with the flags:

```
arm-none-eabi-g++ -std=c++23 -Os -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb
  -ffunction-sections -fdata-sections -Wall -Wextra -Wl,--gc-sections
  --specs=nano.specs --specs=nosys.specs
```

The samples are a default-only console; seven commands with integer, bool, enum and string arguments, and a default handler; and the same with `Help`. All of them feed one fixed input, which has a help request. Only the one with `Help` answers it. Each sample has a stub `Print` that counts bytes and writes no hardware, and the stub's integer writing is in all of them. An empty `main` is the baseline.

The rows come from one size sample, built once per row with the same flags, so they compare with each other.

| Build | text | data | bss |
|---|---|---|---|
| Empty `main` (baseline) | 432 | 8 | 32 |
| Default only | 2108 | 104 | 584 |
| Seven commands, with `Default()` | 11368 | 104 | 836 |
| Seven commands, with `Default()` and `Help` | 16912 | 104 | 912 |
| Row above, with `LineEditing<64>`, `History<4, 128>` and `Completion` | 19720 | 104 | 1136 |
| Row above, with `LongCommands`, `HiddenInput`, `Ansi` and `FloatArguments` listed (the set is unchanged) | 20500 | 104 | 1160 |
| Row above, the set also has a long command, a `Secret` and a `float` argument | 29420 | 104 | 1304 |

The figures are measured with arm-none-eabi-g++ 15.2.1 and the flags above.

`text` includes `.rodata`. `size` gives these figures with `arm-none-eabi-size`. The flags are those above, with `-I` for the stub and the shim.

- The core, with no module listed, links no code of an unlisted feature: `nm` finds no long command, secret, float, `Ansi`, History, LineEditing or Completion symbol. The rest is core code: the refused Ctrl+C handling of the input ring, the console's input path (`Process`, `Accept`, `EndLine`, `Interrupted`) and the inlined `Feed`, which has no symbol of its own. In the rows with arguments, the argument error path is split into separate functions: `RunCommand` and `FillArgument` have their own symbols.
- `Help` costs +5544 bytes of text and +76 bytes of bss, over the seven-command build.
- The editing modules cost +2808 bytes of text and +224 of bss. `LineEditing` and `History` are most of it: +1748 bytes of text and +216 of bss. `Completion` costs +1060 bytes of text and +8 of bss, measured with the row above built without it (that build is not in the table).
- Listing the other four modules, without using them, costs +780 bytes of text and +24 of bss.
- A set that uses a long command, a `Secret` and a `float` costs +8920 bytes of text and +144 of bss. The float number code (`FloatParser`, `CommandFloatSlot`, `NumberDetail` and the `__float` conversions, 25 symbols in `nm`) is 4044 bytes of that text.
- A module that is not listed links nothing. `nm` on the rows without `LongCommands`, `HiddenInput`, `Ansi` and `FloatArguments` finds no module symbol and no float symbol.
- The stub `Print` is in every row, so the figures compare the console with each other, not with a board's total.

Run-time memory, the parts of the console object:
- the input ring, 32 bytes, or the size of a listed `InputRing<N>`, with the two places of refused Ctrl+C presses and their counters (the bss figures above include them);
- the parser's state: a row per group level (the word buffer, 16 bytes, and the name row, at the longest name's length), a row per argument of the largest command (its slot and count), and one value area of the largest command's values;
- the module objects, such as `Help`'s 64-byte line copy, and the editing modules: `LineEditing<64>` is 88 bytes and `History<4, 128>` is 160 bytes, measured on the host (the sizes of the object, not of the G431 build);
- `LongCommands`: the step-state slot, the largest state of the set and its alignment, and the run's name and flag. Without a long command the slot is one byte, and the run's name (16 bytes on the host) is kept once `LongCommands` is listed;
- `HiddenInput`, `Ansi` and `FloatArguments` have no state. Each takes one byte in the module pack. The parser's secret flag is one byte in every console, listed `HiddenInput` or not.

There is no line buffer in the core. `LineEditing` adds one, of its size. Flash holds the compile-time tables of the set (the name trees, the entries and the argument metadata), which are exact-size arrays, and the handler code.

---

## Tests and checks

The sim tests are in `Common/Utilities/CommandLine/.tool/sim`, run by `run.sh` with `-std=c++23 -Wall -Wextra -Werror`:

- `TestCommandTree.cpp`, `TestCommandArguments.cpp`, `TestCommandCalls.cpp`: the declarations, the tables, the readers and the handler calls;
- `TestCommandLine.cpp`: the console, every event and range, the line ends, the quotes, Ctrl+C, the terminal and Backspace at every position of each line;
- `TestCommandRing.cpp`, `TestCommandModules.cpp`: the ring, including refused Ctrl+C presses, their places, the Pop that waits for a due Interrupt, and four threaded runs of 300,000 presses, on rings of 4 and 2 bytes, each from position 0 and from 2^32 - 3, checked against the Interrupts and the bytes, and the module hooks with dummy modules;
- `TestCommandHelp.cpp`: the Help requests, the output of each level and command, and the lines Help must leave alone;
- `TestCommandLineEditing.cpp`: every editing key at the start, middle and end of a line, the buffer limit, Tab as a space, Ctrl+C and the line end clearing the buffer, and Help reading the edited line;
- `TestCommandHistory.cpp`: Up and Down across the entry and byte limits, the draft, empty and repeated lines, and a wrap of the ring;
- `TestCommandCompletion.cpp`: unique, shared and no prefixes, groups and enum values, the second Tab, and Completion with and without `LineEditing`;
- `TestCommandLongCommands.cpp`: a long command's first step at Enter, one step per pass, input that waits, Ctrl+C with and without bytes before it, a failed step, input past the ring, and a closed terminal;
- `TestCommandHiddenInput.cpp`: the echo of a secret as `*`, Backspace, a quoted secret (every character of it hidden), a secret past its capacity, History and secret lines, the tail redraw, and Help;
- `TestCommandAnsi.cpp`: the green prompt, red standard errors, Ctrl+L, a console without Ansi, and a set with its own `Default()`;
- `TestCommandFloatArguments.cpp`: float and double reading, the default, signs and exponents, the range at Enter, infinity and nan in both types, a double with no range, a number that is not one, a missing value, and Help;
- `BuilderErrors/check.sh`: many of the compile-time rules, as snippets that must fail with their message, including `History` without `LineEditing`, `Completion` before `Help`, `Ansi` before `Help`, a long command, a float and a secret without their modules, a float range and default, a ring size of zero, one that is not a power of two, and one above 2^30, and a long state that is not trivially destructible.

---

## Pages

| Page | Header |
|---|---|
| [CommandLine](CommandLine.md) | `CommandLine.h`: the console, `Feed`, `Process`, the events and the echo |
| [CommandHelp](CommandHelp.md) | `CommandHelp.h`: the Help module |
| [CommandLineEditing](CommandLineEditing.md) | `CommandLineEditing.h`: the `LineEditing<N>` module, its buffer, keys and screen |
| [CommandHistory](CommandHistory.md) | `CommandHistory.h`: the `History<Lines, Bytes>` module |
| [CommandCompletion](CommandCompletion.md) | `CommandCompletion.h`: the `Completion` module |
| [CommandLongCommands](CommandLongCommands.md) | `CommandLongCommands.h`: the `LongCommands` feature, the step handler and its rules |
| [CommandHiddenInput](CommandHiddenInput.md) | `CommandHiddenInput.h`: the `HiddenInput` feature and `Secret<N>` |
| [CommandAnsi](CommandAnsi.md) | `CommandAnsi.h`: the `Ansi` module, its colours and Ctrl+L |
| [CommandFloatArguments](CommandFloatArguments.md) | `CommandFloatArguments.h`: the `FloatArguments` feature, `float` and `double` arguments |
| [CommandSet](CommandSet.md) | `CommandSet.h`: the set, its tables and the operations the console uses |
| [CommandSetBuilder](CommandSetBuilder.md) | `CommandSetBuilder.h`: the chain and its checks |
| [CommandArgument](CommandArgument.md) | `CommandArgument.h`: argument declarations and their readers |
| [CommandParser](CommandParser.md) | `CommandParser.h`: the streaming parser, its per-token state and Backspace |
| [CommandInput](CommandInput.md) | `CommandInput.h`: bytes to characters, line ends, escape sequences and keys |
| [CommandRing](CommandRing.md) | `CommandRing.h`: the single-producer, single-consumer input ring |
| [CommandModules](CommandModules.md) | `CommandModules.h`: the module hooks and the feature checks |
| [CommandMessages](CommandMessages.md) | `CommandMessages.h`: the standard default handler's texts |
| [CommandStatusText](CommandStatusText.md) | `CommandStatusText.h`: the status names the standard handler prints |
| [CommandTypes](CommandTypes.md) | `CommandTypes.h`: kinds, faults, events, the call context, `String` and the reader interface |
| [CommandNameTree](CommandNameTree.md) | `CommandNameTree.h`: the prefix tree of names |
| [CommandLevel](CommandLevel.md) | `CommandLevel.h`: the tables of one level, built at compile time |
| [CommandPack](CommandPack.md) | `CommandPack.h`: the value list the builder keeps |
| [CommandCheck](CommandCheck.md) | the compile-time checks of the command line |
