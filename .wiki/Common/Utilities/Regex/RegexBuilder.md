# RegexBuilder

`Common/Utilities/Regex/RegexBuilder.h`: the pattern builder, and the compile-time parser that turns a pattern into a program. The builder holds the pattern text and the options. `RegexCompiled<Make>` evaluates the chain at compile time, so the program is a constant in flash.

---

## Builder

| Member | Does |
|---|---|
| `RegexBuilder(std::string_view)` | Holds the pattern. `explicit`, `constexpr` |
| `IgnoreCase()` | Letters match in either case, in literals and in classes. ASCII only |
| `StackLimit(bytes)` | The stack the loops may take. The default is 4096 bytes |
| `Pattern()` | The pattern text |
| `IsIgnoreCase()` | `true` after `IgnoreCase()` |
| `StackLimitBytes()` | The stack limit in bytes |

A pattern is a captureless lambda that returns a builder. `Regex<Make>` (Regex.md) takes it as `Make`. The chain runs at compile time, and a builder with a bad pattern fails the compile.

```cpp
#include <Utilities/Regex/RegexBuilder.h>


constexpr RegexBuilder Token = RegexBuilder("[a-z]+").IgnoreCase().StackLimit(512);


static_assert(Token.IsIgnoreCase());
static_assert(Token.StackLimitBytes() == 512);
static_assert(Token.Pattern() == "[a-z]+");
```

Limits, all constants in `RegexProgram.h` and `RegexBuilder.h`:

| Limit | Value | Error when over |
|---|---|---|
| Instructions in a program | 1024 | `program is over the instruction limit` |
| Distinct classes | 32 | `program is over the class limit` |
| Capturing groups | 15 | `more than 15 groups` |
| Repeat count (`m`, `n`) | 255 | `repeat count above 255` |
| Stack of the plain loop | `StackLimit` | `matching needs more stack than StackLimit allows` |
| Stack of the captures loop | `StackLimit` | `captures need more stack than StackLimit allows` |

The values are reviewed after the first board run (the design's budget numbers).

---

## Syntax

| Element | Form | Notes |
|---|---|---|
| Literal byte | any byte except `\ . [ ] ( ) { } * + ? \| ^ $` | `\` escapes punctuation |
| Any byte | `.` | Any byte except LF and CR |
| Class | `[abc]`, `[a-z]`, `[^a-z]` | Items are bytes, ranges and class escapes |
| Class escapes | `\d \D \w \W \s \S \t \n \r \f \v \xHH` and escaped punctuation | A shorthand cannot start or end a range |
| Escapes outside a class | the same letters, and escaped punctuation | `\x41` is `A`, `\.` is `.` |
| Anchors | `^` start of the text, `$` end of the text | No multiline mode. An anchor cannot be repeated |
| Groups | `(...)` capturing, `(?:...)` not capturing | `()` and `(?:)` match the empty text |
| Alternation | `a\|b` | Empty alternatives are allowed: `a\|`, `\|a`, `a\|\|b` |
| Repeats | `*  +  ?  {m}  {m,}  {m,n}` | One `?` after a repeat makes it lazy: `*?`, `+?`, `??`, `{m,n}?` |
| Braces | `{m}`, `{m,}`, `{m,n}` right after an atom | Any other `{` or `}` is an error. Escape it as `\{` or `\}` |
| Empty pattern | `""` | Matches the empty text |

Shorthand classes:

| Escape | Set |
|---|---|
| `\d` | `0` to `9` |
| `\w` | `A` to `Z`, `a` to `z`, `0` to `9`, `_` |
| `\s` | space, `\t`, `\n`, `\v`, `\f`, `\r` |

The upper-case form (`\D`, `\W`, `\S`) is the complement of the set.

Rules:

- `m` and `n` are at most 255, and `m <= n`.
- In a class, `]` is escaped, and `[]` and `[^]` are errors (an empty class). `-` first or last is a literal. `^` is a literal unless it is first.
- `[` inside a class is a literal, as in ECMAScript.
- `\0` is an error: write `\x00`. `\x` takes two hex digits.
- An escape of ASCII punctuation is that byte, so `\_` and `\]` are allowed. Any other escape is an error.
- `a**`, `a+*` and `a???` are errors. `^*` and `$+` are errors.
- `(?` followed by anything but `:` is an error.
- `IgnoreCase()` folds before negation: with it, `[^a]` excludes `a` and `A`.

Patterns are written as raw string literals (`R"(...)"`) so that `\d` and `\.` need no doubled backslash.

```cpp
#include <Utilities/Regex/Regex.h>
#include <string_view>


// \x41 is A, \. is a dot, \d is a digit, and \] is the bracket. The class holds a digit, an underscore and the range a to f.
using Escapes = Regex<+[] {
	return RegexBuilder(R"(\x41\.[\d_a-f]+\])");
}>;


bool EscapesMatch() {
	return Escapes::Matches("A.9f]");
}


// The lazy + stops at the first >, so the first tag of "<a><b>" is "<a>".
using Tag = Regex<+[] {
	return RegexBuilder(R"(<.+?>)");
}>;


std::string_view FirstTag(std::string_view text) {
	Result<Tag::Captures> found = Tag::Find(text);
	if (!found.IsOk()) {
		return std::string_view();
	}

	return found.Value().Group(0);
}
```

---

## Compile errors

A pattern that breaks a rule fails the compile. The error is a call to a function that is declared and never defined, so the compiler prints the message and the value. The value is the offset in the pattern (counted from 0), or a count, or a number of bytes, as the last column says.

| Message | When | Value |
|---|---|---|
| `unbalanced ')'` | A `)` with no `(` | Offset of the `)` |
| `missing ')'` | A `(` never closed | Offset of the `(` |
| `missing ']'` | A `[` never closed | Offset of the `[` |
| `empty class` | `[]` or `[^]` | Offset of the `[` |
| `reversed range` | A range such as `[z-a]` | Offset of the range |
| `a class escape cannot start or end a range` | A shorthand at a range end, such as `[\d-z]` | Offset |
| `nothing to repeat` | A repeat with no atom before it, after an anchor, or after another repeat | Offset |
| `repeat count above 255` | `m` or `n` above 255 | Offset |
| `repeat minimum above maximum` | `{m,n}` with `m > n` | Offset |
| `escape '{' and '}' that are not quantifiers` | A `{` or `}` that is not a whole repeat | Offset |
| `escape ']' outside a class` | A `]` not escaped and not in a class | Offset |
| `unknown escape` | `\q`, `\ ` and other escapes not in the table | Offset |
| `use \x00 for NUL` | `\0` | Offset |
| `\x takes two hex digits` | `\x` without two hex digits | Offset |
| `pattern ends with a backslash` | A `\` at the end | Offset |
| `backreferences are not supported` | `\1` to `\9` | Offset |
| `lookaround is not supported` | `(?=`, `(?!`, `(?<=`, `(?<!` | Offset |
| `named groups are not supported` | `(?<name>` | Offset |
| `inline flags are not supported, use IgnoreCase()` | `(?` followed by a flag letter, such as `(?i)` | Offset |
| `unknown group syntax` | `(?` followed by anything else but `:` | Offset |
| `word boundaries are not supported` | `\b` or `\B` | Offset |
| `more than 15 groups` | The 16th `(` | Offset of that `(` |
| `pattern has too many parts` | More than 2048 items in the pattern | The limit |
| `program is over the instruction limit` | More than 1024 instructions | The instruction count |
| `program is over the class limit` | A 33rd distinct class | 33 |
| `matching needs more stack than StackLimit allows` | `PlainStackBytes` above `StackLimit` | Bytes needed |
| `captures need more stack than StackLimit allows` | `CapturesStackBytes` above `StackLimit` | Bytes needed |

Two errors come from the code and not the pattern text. `the program fill does not match its count` means the second pass wrote a different number of instructions than the first pass counted. It cannot come from pattern text, so no snippet triggers it.

The instruction-limit value is the exact count, and the class-limit value is the first class over the limit, not the total.

Example of a pattern that fails: `RegexBuilder("ab)")` fails with `unbalanced ')'` and value `2`.
