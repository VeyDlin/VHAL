# Regex

`Common/Utilities/Regex/Regex.h`: the matching functions of a compiled pattern. `Regex<Make>` takes a pattern as a template argument. `Make` is a captureless lambda that returns a `RegexBuilder` (see RegexBuilder.md). Each member checks the stack of its own loop at compile time.

---

## Members

| Member | Returns | Loop | Stack checked |
|---|---|---|---|
| `Matches(text)` | `bool`: the whole text matches | plain | `PlainStackBytes` |
| `Contains(text)` | `bool`: some part of the text matches | plain | `PlainStackBytes` |
| `Find(text, from = 0)` | `Result<Captures>` | captures | `CapturesStackBytes` |
| `PlainStackBytes` | `std::size_t`, `static constexpr` | | |
| `CapturesStackBytes` | `std::size_t`, `static constexpr` | | |
| `CaptureFrames` | `std::size_t`, `static constexpr`: the closure frames of the capture loop, 4 bytes each | | |
| `GroupCount` | `std::size_t`, `static constexpr`: the groups, not counting group 0 | | |

For a text up to 65534 bytes, `Contains(text)` gives the same answer as `Find(text).IsOk()`. Above that, `Find` gives `outOfRange` and `Contains` still searches. `Matches` implies `Contains`.

`Find` returns the first match that starts at offset `from` or later, by leftmost-first order:

| Status | When |
|---|---|
| `noMatches` | No match from `from` on |
| `invalidParameter` | `from` is past the end of the text |
| `outOfRange` | The text is over `RegexMaxFindInput` (65534) bytes |

`^` still means the start of the whole text, and `$` the end of the whole text, so `from` does not move an anchor. The pattern `^a` finds nothing in `"aa"` from offset 1.

---

## Captures

`Find` returns a `Captures` object. It holds the offsets of group 0 (the whole match) and of each group. Group 0 is always set in a successful `Find`, and a group that did not take part in the match gives an empty view.

| Member | Returns |
|---|---|
| `Begin()` | `std::size_t`: the offset of the match in the text |
| `End()` | `std::size_t`: the offset just past the match |
| `Group<I>()` | `std::string_view` of group `I`. `I` is a template argument, so an index above `GroupCount` does not compile |
| `Group(i)` | `std::string_view` of group `i` at run time. An index above `GroupCount` gives an empty view |
| `Matched(i)` | `bool`: group `i` took part in the match. Above `GroupCount` gives `false` |

The views point into the text that `Find` was given, so the text must outlive the `Captures`.

---

## Examples

### Matches and Contains

```cpp
#include <Utilities/Regex/Regex.h>
#include <string_view>


// A date as YYYY-MM-DD.
using Date = Regex<+[] {
	return RegexBuilder(R"([0-9]{4}-[0-9]{2}-[0-9]{2})");
}>;


bool IsDate(std::string_view text) {
	return Date::Matches(text);
}


// True when a date appears somewhere in the text.
bool HasDate(std::string_view text) {
	return Date::Contains(text);
}
```

### Find and the groups

```cpp
#include <Utilities/Regex/Regex.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <string_view>


// A name, an equals sign and a number. Group 1 is the name and group 2 the number.
using Assignment = Regex<+[] {
	return RegexBuilder(R"(([a-z_]+)=([0-9]+))");
}>;


static_assert(Assignment::GroupCount == 2);


// The value of the first assignment in the text, or 0 when there is none.
uint32 FirstValue(std::string_view text) {
	Result<Assignment::Captures> found = Assignment::Find(text);
	if (!found.IsOk()) {
		return 0;
	}

	uint32 value = 0;
	for (char digit : found.Value().Group<2>()) {
		value = value * 10 + static_cast<uint32>(digit - '0');
	}

	return value;
}
```

### Optional groups

```cpp
#include <Utilities/Regex/Regex.h>
#include <Utilities/Result/Result.h>
#include <string_view>


// A word, then an optional colon and a number. Group 2 is the number.
using Labelled = Regex<+[] {
	return RegexBuilder(R"([a-z]+(:([0-9]+))?)");
}>;


// True when the text has a number after its colon.
bool HasNumber(std::string_view text) {
	Result<Labelled::Captures> found = Labelled::Find(text);
	if (!found.IsOk()) {
		return false;
	}

	return found.Value().Matched(2) && found.Value().Group(2).size() > 0;
}
```

### Every match in a text

`Find` takes a start offset, and the caller moves it on to the end of each match. This pattern never matches the empty text, so each match moves the offset forward. A pattern that can match the empty text needs one more step after an empty match, or the loop stays in place.

```cpp
#include <Utilities/Regex/Regex.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <string_view>


// A word of letters, at least one.
using Word = Regex<+[] {
	return RegexBuilder(R"([A-Za-z]+)");
}>;


// The number of words in the text. A text over RegexMaxFindInput bytes gives outOfRange, and so does any error other than noMatches.
Result<std::size_t> CountWords(std::string_view text) {
	std::size_t count = 0;
	std::size_t from = 0;
	while (true) {
		Result<Word::Captures> found = Word::Find(text, from);
		if (!found.IsOk()) {
			if (found.Error() == ResultStatus::noMatches) {
				return Ok<std::size_t>(count);
			}

			return Err<ResultStatus>(found.Error());
		}

		count++;
		from = found.Value().End();
	}
}
```

### Stack of a pattern

`PlainStackBytes` and `CapturesStackBytes` can be checked in the code that uses the pattern:

```cpp
#include <Utilities/Regex/Regex.h>


using Token = Regex<+[] {
	return RegexBuilder("[a-z][a-z0-9_]{0,15}").IgnoreCase().StackLimit(1024);
}>;


static_assert(Token::PlainStackBytes <= 1024);
static_assert(Token::CapturesStackBytes <= 1024);
```

---

## Limits

- `RegexMaxFindInput` is 65534. Capture offsets are 16 bits, and 0xFFFF means unset.
- The plain loop takes any input length. Its time grows with the input length times the program size.
- Each `Regex<>` type is its own type with its own program. The two loops are shared, so a second pattern adds only its program, its classes and a small wrapper.
