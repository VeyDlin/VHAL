# CommandFloatArguments

`Common/Utilities/CommandLine/CommandFloatArguments.h`: float and double arguments, read by TextNumber's `FloatParser` (`Common/Utilities/TextNumber`). Listed as `CommandLine<Set, App, Help, FloatArguments>`.

```cpp
ResultStatus Speed(CommandCall<AppContext> call, double gain, float rpm);

using AppCommands = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("speed", "set the speed", &Speed,
			Argument<double>("gain").Range(0.0, 10.0),
			Argument<float>("rpm").Range(-1.5f, 2.5f).Default(0.5f));
}>;
```

---

## Declaration

- `Argument<float>` and `Argument<double>`: `Range(low, high)` and `Default(value)`. `Range` takes the same type as the argument.
- `Range` checks that neither bound is infinite or nan (`a float range bound is not finite`), and that the minimum is not above the maximum. `Default` must lie inside the range. All are compile errors with their messages.
- An unranged float has the whole type's finite range (`lowest()` to `max()`). Only finite values are accepted: a range has finite bounds, so no value of infinity or nan lies inside it.
- A float with a default is optional and comes after the required arguments, as any optional argument does.

---

## Reading and errors

- The parser takes a sign, a point and an exponent, and `inf` and `nan` in any case, as `FloatParser` does. A `+` is accepted.
- A character that cannot be part of a number is refused as it is typed, and reported at Enter: `speed: gain must be a number`.
- A value outside the range is reported at Enter: `speed: rpm must be -1.50..2.50`. The range is written with two decimals.
- Infinity and nan are read, and refused as out of range, both as a float and as a double, and at either sign. The message is the range one: `speed: gain must be 0.00..10.00` for `speed inf`. Infinity is not a malformed number; it is a number outside every finite range. A nan is outside every range too, since no comparison with nan holds.
- With no `Range`, `free inf` and `free nan` are refused the same way, and the message names the type's finite range, written in exponent form with two decimals: `free: value must be -1.80e+308..1.80e+308` for a double, and `freef: value must be -3.40e+38..3.40e+38` for a float.
- A missing required float is `speed: gain is missing`, as for an integer.
- Help writes `number 0.00..10.00`, and `default 0.50` for an optional one.

---

## Feature, not hook

The parser is the `CommandParserOf` hook: `CommandParserOf<float>` and `<double>` name `CommandFloatParser<T>`, which pushes characters through `FloatParser`, undoes them with `Pop`, and gives the value with `Finish(T&)`. The range is checked by the reader (`CommandFloatSlot`), which holds it from the declaration.

A float argument needs three things:

| Missing | Result |
|---|---|
| `CommandFloatArguments.h` included | `a float argument needs CommandFloatArguments.h, and the FloatArguments module listed in the console` (from the primary `Argument` template) |
| The module not listed | `a float argument needs the FloatArguments module, listed in the console` (from the console) |

Include the header before the set that uses it, since a template is instantiated at its first use. A set with no float argument has no float code, whether or not the header is included, and the module costs nothing then.

When a float argument is refused, the event carries its range in `range.floats` (CommandTypes.h). That shares storage with the integer range, `range.integers`, so the event has the same size whether or not the set has a float argument. The range text is written only by a set with float arguments, so a set without them links no float formatting from the standard messages.

---

## Structure

| Part | Use |
|---|---|
| `CommandFloatParser<T>` | The parser, through `CommandParserOf` |
| `CommandFloatSlot<T>` | The reader: parser, the range and the refusal |
| `Argument<T>` (floating point) | The declaration: `Kind` is `Float`, `Range`, `Default`, `Verify` |
| `CommandSlotOf<Argument<T>>` (floating point) | Selects `CommandFloatSlot` |
| `struct FloatArguments` | The module. `Provides` is `CommandFloatFeature` |

The float code (`FloatParser`) is large: a set that uses a float adds several kilobytes of flash. Measurements are in README.md, Memory.
