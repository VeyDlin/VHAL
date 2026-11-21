# CommandArgument

`Common/Utilities/CommandLine/CommandArgument.h`: the argument declarations (`Argument<T>`, `EnumArgument`), the metadata the console reads (`CommandArgumentInfo`), and the readers of argument text (`CommandSlot` subclasses).

---

## Declarations

| Declaration | Kind | What it holds |
|---|---|---|
| `Argument<T>` for an integer `T` | `Integer` | Name, minimum, maximum, default |
| `Argument<bool>` | `Boolean` | Name, default |
| `Argument<E>` for an enum `E` | `Enumeration` | Name. `Values({...})` gives an `EnumArgument<E, N>` of exact size |
| `Argument<String<N>>` | `Text` | Name, default text |
| `Argument<Secret<N>>` (HiddenInput) | `Secret` | Name. No default. Echoed as `*` |
| `Argument<float>`, `Argument<double>` (FloatArguments) | `Float` | Name, range, default |
| `Argument<T>` for any other `T` | `Custom` | Name, default |

A floating-point `T` with no FloatArguments header is refused by the primary template, with `a float argument needs CommandFloatArguments.h, and the FloatArguments module listed in the console`. See CommandFloatArguments.md.

`Range(low, high)` takes integers of any type and checks that each bound fits the argument's type. `Default` takes an integer, a `bool`, an enum value, or text. Each declaration's `Verify()` runs when the command is declared, so `Range` and `Default` may come in either order.

---

## Readers

A reader is a `CommandSlot` subclass. The console creates one per argument with placement new. The reader gets the argument's text one character at a time:

- `Push(c)` returns `false` when the character is refused. After a refusal every later character is refused until `Pop` removes the refused ones, so `Pop` always undoes the last character the console gave.
- `Pop()` undoes the last character.
- `Finish(destination)` returns a `CommandFault`. `None` means the value was written to `destination`, which is the argument's place in the value area.

| Reader | Accepts |
|---|---|
| `CommandIntegerSlot<T>` | Decimal, `0x` and `0b`, and a sign for a signed type or for `+`. Parsed by `IntegerParser` (`TextNumber`). The range is checked at `Finish`, which gives `OutOfRange`, and a text the parser refused gives `Invalid` |
| `CommandBoolSlot` | `on`, `off`, `true`, `false`, `1`, `0`. A character is kept only when some word starts with it |
| `CommandEnumSlot<E, N>` | Walks the enum's value names in the level's tree. Keeps the typed characters, so `Pop` walks back from the root. `Finish` is `Invalid` for a name that is not complete |
| `CommandTextSlot<N>` | Stores the characters as typed, quotes included, up to the capacity. `Quoting()` is true while a quoted text is open, so a space is part of it. `Finish` gives the text without the quotes, with `\"` and `\\` resolved and any other `\x` kept as typed. An open quote is `MissingQuote`, and text after the closing quote is `Invalid`. Past the capacity, `TooLong` |
| `CommandCustomSlot<T>` | Delegates to `CommandParserOf<T>::Type` |

Every reader keeps a `refused` count, so `Pop` after a refusal removes the refusal and not a character that was kept.

---

## Custom types and modules

A custom argument type `T` names its parser as `T::CommandParser`, or a module specializes `CommandParserOf<T>`:

```cpp
struct Channel {
	int value = 0;

	struct CommandParser {
		bool Push(char character);
		void Pop();
		bool Finish(Channel& channel) const;
	};
};

// In a float module's header, without touching the core:
template<>
struct CommandParserOf<float> {
	using Type = FloatArgumentParser;
};
```

The parser must be trivially destructible, and `T` trivially copyable. `Finish` must be `const`.

---

## Metadata

`CommandArgumentInfo` holds what the console needs about one argument: `name`, `kind`, `minimum`, `maximum` (as `int64`, with 64-bit unsigned maximums above `INT64_MAX` clamped; the exact value is in `unsignedMaximum`, with `maximumUnsigned` set), `hasDefault`, `valueOffset`, `valueAlign`, `root` (the enum tree in the level), and `slotBytes` and `slotAlign`. For a text argument `maximum` is the capacity. The console's standard texts use these fields, and the Help module reads the typed declarations instead, so it prints each type's real limits and the enum's value names. The metadata has no enum value names and no default values.
