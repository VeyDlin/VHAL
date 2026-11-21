# Command Line

`Common/Utilities/CommandLine/`: a text console for any byte stream (UART, USB CDC-ACM, or anything with a `Print`). Commands are declared once, with typed arguments, and the compiler checks the declaration. The console parses input as it arrives, so no line is kept. No heap is used, and no exception, RTTI or `std::function`.

Status: the declaration side, the console and the `Help` module are done. The other modules (`LineEditing`, `History`, `Completion`, `LongCommands`, `HiddenInput`, `Ansi`, `FloatArguments`) are not written. Their hooks are in place (see Modules).

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

The rules are checked by `Common/Utilities/CommandLine/.tool/sim/BuilderErrors/check.sh`, one snippet per rule.

---

## Running a console

```cpp
CommandLine<AppCommands, App> console(app, out);            // out is a Print, app the application
CommandLine<AppCommands, App, InputRing<64>, Help> console(app, out);  // a ring of 64 bytes, and a listed module
```

| Call | Context it may run in | What it does |
|---|---|---|
| `Feed(byte)` | an interrupt, or the main loop | Calls the `OnInterrupt` handler when the byte is Ctrl+C. Stores the byte in the input ring. Never prints, and calls no other handler |
| `Process()` | the main loop or a task, never an interrupt | Drains the ring, one byte at a time, so the echo follows the input. Raises events, runs handlers and module hooks, and writes all output |
| `SetTerminalOpen(open)` | any context | Stores the state. The `TerminalOpened` or `TerminalClosed` event comes from the next `Process` |
| `Output()`, `Application()` | `Process` (from a module hook) | The console's `Print` and application, for modules |

- `Feed` is single-producer. Only one context may call it at a time, and `Process` is the only consumer. The ring is 32 bytes, or the size of a listed `InputRing<N>`.
- Bytes that do not fit the ring are counted and reported as `InputOverflow` at the next `Process`.
- Ctrl+C (`0x03`) is handled in `Feed`. An `OnInterrupt` handler runs at once, with no output. The byte is then queued like any other, so `Process` cancels the line where the byte was. A Ctrl+C that is lost because the ring is full still cancels the line, at its place: the bytes accepted before it are handled first, as usual.
- Memory: see Memory below.

### Input

- Printable characters are echoed as they arrive. Tab is a space.
- Backspace (`0x08`) and DEL (`0x7F`) erase: the echo is `BS SPACE BS`, and only when something was erased.
- Line ends: CR, LF, CR LF and LF CR each end one line, and two CRs or two LFs in a row end two lines. Enter echoes `\r\n`.
- Escape sequences are decoded and dropped: arrow keys, Home, End and Delete inject nothing. A control byte inside a sequence breaks it and is read as input. The keys go to a module's `OnKey` hook first.
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
| `Word` | the default-only build: a word is final, when the next word starts or at Enter | (nothing) |
| `EndOfLine` | the default-only build: at Enter, after the words | (nothing) |
| `Interrupt` | Ctrl+C | `^C`, then the prompt |
| `TerminalOpened` | `SetTerminalOpen(true)` | `VHAL command line`, then the prompt |
| `TerminalClosed` | `SetTerminalOpen(false)`: the line is dropped | (nothing) |
| `InputOverflow` | bytes were lost to a full ring | `input lost` |

A `CommandEvent` has `kind`, `text` (the word of `UnknownCommand` and `Word`), `status` (of a failed command), `command` (the command an error is about), `argument` (the argument's declared name), `argumentKind`, `fault` (why it was refused), and `minimum` and `maximum` (its range; `unsignedMaximum`, with `maximumUnsigned` set, holds a 64-bit unsigned maximum above the int64 range), and `truncated` on a `Word` event whose word is longer than the word buffer. A string that is not about an argument leaves the argument fields empty. The command is named by its own name, not by the group path: `gain: value must be ...`, not `motor pid gain: ...`.

With a `Default()`, the set's handler decides what each event prints or does, and the standard texts are not used.

### Default-only build

A set with no commands (`CommandSetBuilder().Default(&OnEvent)`, or none at all) has no tree. It splits the input into words and raises:

- `Word` when a word is final: when the next word starts, or at Enter for the last one. The text is the word, cut to its first 16 characters, and `truncated` is set when the word was longer.
- `EndOfLine` at Enter, after the words, or `EmptyLine` for a line with no word.

Backspace removes characters from the last word. While only spaces follow a word, it erases the spaces and then the word. Once a later word has started, the earlier word is final, and the space that keeps the two apart stays. So after `a b`, two Backspaces leave `a `, and a third does nothing: `a` cannot be erased from that position. This is the rule as built; the owner has not yet decided whether it should change.

### Interrupt

With `OnInterrupt`, Ctrl+C runs the handler in `Feed`, at once, from whatever context `Feed` runs in. The handler gets `App&` only, and must be short and safe in an interrupt. It is a convenience, not a safety function: an emergency stop that matters must not depend on the console.

Either way, `Process` cancels the line where the byte was, and an `Interrupt` event goes out. A cancelled line is not answered: a `help` typed before the Ctrl+C prints nothing.

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
	void OnPrompt(Console& console);                         // before the prompt
};
```

Each hook is optional. A `void OnLineEnd` is also accepted. The console passes itself, so a module writes with `console.Output()` and reaches the application with `console.Application()`. Every module gets each line end, so each can reset its own state. The other hooks stop at the first module that takes the character, key or event.

A module whose `Needs` no listed module provides is a compile error, `a module needs a feature that no listed module provides`.

`CommandModules.h` has the dispatch. `CommandLine.h` shows where each hook runs.

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

Help keeps its own copy of the line, up to 64 characters (`LineSize`), because the console keeps none. Backspace removes from the copy as it does from the console. A line longer than that is not a request, and it runs as a normal line. Lost input (`InputOverflow`), Ctrl+C and a terminal change drop the copy.

Limits:
- Help must be listed before any module that takes characters or events. A module listed earlier that takes one hides it from Help, so the copy would differ from the console's.
- A set with no commands and no groups has nothing to describe: a listed `Help` is a compile error.

---

## Memory

Measured on the Cortex-M4 with Arm GNU Toolchain 15.2.1, with the flags:

```
arm-none-eabi-g++ -std=c++20 -Os -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb
  -ffunction-sections -fdata-sections -Wall -Wextra -Wl,--gc-sections
  --specs=nano.specs --specs=nosys.specs
```

The samples are a default-only console; seven commands with integer, bool, enum and string arguments, and a default handler; and the same with `Help`. All of them feed one fixed input, which has a help request. Only the one with `Help` answers it. Each sample has a stub `Print` that counts bytes and writes no hardware, and the stub's integer writing is in all of them. An empty `main` is the baseline.

| Build | text | data | bss | console object (bss) |
|---|---|---|---|---|
| Empty `main` (baseline) | 432 | 8 | 32 | |
| Default only | 1888 | 104 | 512 | 156 |
| Seven commands, with `Default()` | 10952 | 104 | 808 | 400 |
| Seven commands, with `Default()` and `Help` | 15352 | 104 | 884 | 472 |

`text` includes `.rodata`. `size` gives these figures with `arm-none-eabi-size`.

- Help costs +4400 bytes of text, and +76 bytes of bss, of which 72 are in the console object (its 64-byte line copy and its length).
- The seven-command set costs +9064 bytes of text over the default-only build. That is the parser's readers and the tables of the seven commands.
- Outside the console object, the samples' bss is 356 bytes (default only), 408 (seven commands) and 412 (with Help), against 32 for the empty baseline. The first reading attributed the difference to newlib state (`__sf` and `_impure_data`), which the baseline does not link. The cause is not identified, and it is not command line memory.
- The stub `Print` is included in every sample, so the figures compare the console with each other, not with a board's total.

Run-time memory, the parts of the console object:
- the input ring, 32 bytes, or the size of a listed `InputRing<N>`;
- the parser's state: a row per group level (the word buffer, 16 bytes, and the name row, at the longest name's length), a row per argument of the largest command (its slot and count), and one value area of the largest command's values;
- the module objects, such as `Help`'s 64-byte line copy.

There is no line buffer in the core. Flash holds the compile-time tables of the set (the name trees, the entries and the argument metadata), which are exact-size arrays, and the handler code.

---

## Tests and checks

The sim tests are in `Common/Utilities/CommandLine/.tool/sim`, run by `run.sh` with `-std=c++20 -Wall -Wextra -Werror`:

- `TestCommandTree.cpp`, `TestCommandArguments.cpp`, `TestCommandCalls.cpp`: the declarations, the tables, the readers and the handler calls;
- `TestCommandLine.cpp`: the console, every event and range, the line ends, the quotes, Ctrl+C, the terminal and Backspace at every position of each line;
- `TestCommandRing.cpp`, `TestCommandModules.cpp`: the ring, and the module hooks with dummy modules;
- `TestCommandHelp.cpp`: the Help requests, the output of each level and command, and the lines Help must leave alone;
- `BuilderErrors/check.sh`: each compile-time rule, as a snippet that must fail with its message.

---

## Pages

| Page | Header |
|---|---|
| [CommandLine](CommandLine.md) | `CommandLine.h`: the console, `Feed`, `Process`, the events and the echo |
| [CommandHelp](CommandHelp.md) | `CommandHelp.h`: the Help module |
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
| [CommandCheck](CommandCheck.md) | `CommandCheck.h`: the compile-time error mechanism |
