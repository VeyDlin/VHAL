# Regex

`Common/Utilities/Regex/`: a small regular-expression matcher. The pattern is a template argument, and it is compiled at compile time into a program in flash. Matching takes time linear in the input length, times the program size (and times the slot count for `Find`), with no backtracking. No heap, exception, RTTI, `std::function` or `VSTD` is used. A bad pattern is a compile error that prints a message and a value.

Status: the builder, the parser, both matching loops and the host tests are done, and the sizes are measured on the Cortex-M4 target. The board demo (`.demo/STM32G4/Application/DemoRegex`) builds for the G431 in two variants, but it is not yet run on a board. No caller in the repository uses the matcher yet.

Pages: [Regex](Regex.md) for the matching functions, [RegexBuilder](RegexBuilder.md) for the pattern syntax and the compile errors.

---

## Example

A URL check that keeps the host, and a status line check that needs no captures. The file compiles on the host (see Tests).

```cpp
#include <Utilities/Regex/Regex.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <string_view>


// An absolute http or https URL. Group 1 is the scheme, group 2 the host and group 3 the optional port.
inline constexpr RegexBuilder (*UrlPattern)() = +[] {
	return RegexBuilder(R"(^(https?)://([a-z0-9.-]+)(?::([0-9]{1,5}))?(/[^ ]*)?$)")
		.IgnoreCase()
		.StackLimit(3072);
};

using Url = Regex<UrlPattern>;


ResultStatus Open(std::string_view text) {
	Result<Url::Captures> found = Url::Find(text);   // noMatches when the text is not a URL
	if (!found.IsOk()) {
		return found.Error();
	}

	// The regex checks the shape of the host. The length rule is the caller's.
	std::string_view host = found.Value().Group<2>();
	if (host.size() > 253) {
		return ResultStatus::invalidParameter;
	}

	return ResultStatus::ok;
}


// Matches runs the plain loop only, so the captures loop is not linked.
using StatusLine = Regex<+[] {
	return RegexBuilder(R"(HTTP/1\.[01] [0-9]{3}( .*)?)");
}>;


bool IsStatusLine(std::string_view line) {
	return StatusLine::Matches(line);
}
```

- A raw string literal (`R"(...)"`) lets the pattern use `\d` and `\.` without doubled backslashes.
- `RegexBuilder` is a builder in the style of the other VHAL builders. The lambda is captureless, and its chain is evaluated at compile time. Its type is `RegexBuilder (*)()`.
- The port range (1 to 65535) and the host length are code rules, not regex rules. The pattern accepts `65536`, and the caller rejects it.

---

## Contracts

- **Context.** Any. The matcher has no global state and no lock, so it can run from several places, and it can run from an interrupt when the stack has room for its workspace (see Memory).
- **Input and output.** Input is a `std::string_view`. A captured group is a view into the input, so the caller keeps the input alive while it uses the result.
- **Results.** `Matches` and `Contains` return `bool`. `Find` returns `Result<Captures>` with `noMatches`, `invalidParameter` (the start offset is past the end) or `outOfRange` (the input is over 65534 bytes, see Regex.md).
- **Semantics.** Leftmost-first, as ECMAScript and `std::regex_search`: `a|ab` finds `a` in `ab`. `Matches` asks for the whole input, as `std::regex_match` does.
- **Pattern errors** are compile errors only. There is no run-time pattern error, and patterns are never read at run time.
- **Captures are opt-in.** The capture loop is emitted only when `Find` is called. A program that calls only `Matches` and `Contains` has no capture code (checked by `CheckNotEmitted.sh`).
- **Not in version 1.** Backreferences, lookaround, named groups, inline flags, word boundaries, multiline mode, Unicode, and matching input in pieces.

---

## Memory

The program is in flash. A pattern costs 6 bytes per instruction, 32 bytes per distinct character class, and 8 bytes for the failure record, rounded up to a multiple of 4. The `View` of a pattern is 16 bytes. A pattern that no code uses costs nothing.

Measured on the Cortex-M4 target (`arm-none-eabi-g++ -std=gnu++23 -mcpu=cortex-m4 -mthumb -O3`, the sizes read from the ELF symbols):

| Pattern | Instructions | Classes | Groups | Image (flash) | Plain stack | Captures stack |
|---|---|---|---|---|---|---|
| `HTTP/1\.[01] [0-9]{3}( .*)?` | 21 | 2 | 1 | 200 bytes | 50 bytes | 480 bytes |
| The URL pattern above, with `IgnoreCase()` | 40 | 3 | 4 | 344 bytes | 96 bytes | 1952 bytes |
| 1024 instructions, one class | 1024 | 1 | 0 | 6184 bytes | 2304 bytes | not used |
| 1024 instructions, 32 classes | 1024 | 32 | 0 | 7176 bytes | 2304 bytes | not used |

The loops, measured on the target (`-Os`, one object each, the pattern image included):

| Object | Text (flash) |
|---|---|
| One pattern, `Matches` only | 568 bytes |
| One pattern, `Matches` and `Find` | 1544 bytes |
| Two patterns, `Matches` only | 1190 bytes |
| Two patterns, `Matches` and `Find` | 2508 bytes |

The board demo (`-O3`) links to 54320 bytes of flash with `Matches` only, and 58016 bytes with `Find` (the difference is the capture loop and the calls to `Find`). RAM is 3168 bytes in both builds. These are the whole-demo figures, not the matcher alone.

The stack that a call takes is set by the pattern:

| Loop | Stack |
|---|---|
| `Matches` and `Contains` | `PlainStackBytes = 8W + 2N` |
| `Find` | `CapturesStackBytes = 2 * (2N + 2NS + 4W) + 4F` |

`N` is the instruction count, `W = ceil(N / 32)`, `S = 2 * (groups + 1)` and `F` is `CaptureFrames`, the most frames the closure stack of the capture loop can hold. `F` is counted from the program: 1 for the start, then for each instruction 1 for a jump, begin or end, 2 for a split or a save, and 1 plus the number of slots for a clear. A consuming instruction and the match count nothing. The count is an upper bound, so the real stack use can be smaller. A pattern's stack must fit its `StackLimit()`, which is 4096 bytes by default. A pattern that does not fit is a compile error. Each call's workspace is a local of that call, so it lives in the frame of that call and nowhere else. The entry points `Matches`, `Contains` and `Find` are marked `_ANoinline`, so the optimizer cannot fold two of them into one frame. Without that, the workspaces of the calls in one function add up in a single frame. The stack a caller needs is the largest workspace of the calls it makes, plus the frames of the call chain: the caller, the entry point's own frame without its workspace, and the loop frames that are not inlined into it. An interrupt can arrive at the deepest point, so add its exception frame (32 bytes on a Cortex-M, 104 bytes on a Cortex-M4F when the FPU context is stacked) and the handler's own frame.

The frames of the matching loops and of the entry points, from `-fstack-usage` at `-O3` for the Cortex-M4 (`-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard`). The loops are the out-of-line copies, so their frames are the cost when GCC does not inline them. The entry points are the frames of the calls with the status-line pattern (workspace 50 bytes for `Matches`, 480 bytes for `Find`) and the URL pattern (96 bytes and 1952 bytes):

| Function | Frame | Note |
|---|---|---|
| `RegexRunPlain` | 104 bytes | plain loop, out of line |
| `RegexAddPlainThread` | 32 bytes | plain closure, out of line |
| `RegexRunCaptures` | 152 bytes | captures loop, out of line |
| `RegexAddCaptureThread` | 56 bytes | captures closure, out of line |
| `Matches`, status line | 120 bytes | workspace 50 bytes |
| `Matches`, URL | 176 bytes | workspace 96 bytes |
| `Contains`, status line | 128 bytes | workspace 50 bytes |
| `Contains`, URL | 160 bytes | workspace 96 bytes |
| `Find`, status line | 648 bytes | workspace 480 bytes |
| `Find`, URL | 2176 bytes | workspace 1952 bytes |

Stack on the board is checked by the demo, which paints the free RAM and prints the stack used. The budget is the largest workspace of the calls it makes plus the margin of the chain: 288 bytes for `Matches` (`main` 8, `Run` 56, the entry point without its workspace 80) and 480 bytes for `Find` (`main` 8, `Run` 112, the entry point without its workspace 224). Both margins add 104 bytes for the exception frame of an interrupt with the FPU context and 32 bytes for the handler's own frame, see `StackMarginBytes` in `DemoRegex.cpp`. On the STM32G431 the demo measured 344 bytes for `Matches` (96 of workspace) and 2400 bytes for `Find` (1952 of workspace): in both, the chain's frames plus 104 bytes, with all 40 rows matching.

---

## Known differences from std::regex

The host tests compare the matcher with `std::regex` (ECMAScript). These differ:

- **Empty iterations.** ECMAScript stops a quantified group that matches the empty string. This matcher keeps the iteration, so the span of the match, and the captures, can differ.
- **Captures of a repeated group.** `libstdc++` keeps a capture from an earlier iteration of a quantified group: `((a)|b)+` on `ab` gives group 2 as `a`. ECMAScript and this matcher unset it, so group 2 is empty.
- **Carriage return.** `.` does not match LF or CR, as in ECMAScript without the `s` flag. The libraries disagree on CR, so the tests avoid it.
- **Case.** `IgnoreCase()` folds ASCII letters only. `std::regex` uses the locale.
- **Classes.** `\s` is space, `\t`, `\n`, `\v`, `\f` and `\r`. `\w` and `\d` are ASCII. There are no Unicode classes.
- **Limits.** A repeat count is at most 255. A pattern has at most 1024 instructions, 32 classes and 15 groups. `std::regex` has none of these limits.
- **No `Find` all.** The caller loops with `from`, as shown in Regex.md.
- **Find input.** `Find` takes inputs of at most 65534 bytes, because capture offsets are 16 bits. `Matches` and `Contains` take any length.

---

## Tests

`Common/Utilities/Regex/.tool/sim/run.sh` runs the host checks: the syntax table, the generated patterns compared with `std::regex`, the linear-time ratio, the no-heap run, the stack formulas, the edge cases, the compile-time error snippets, the wiki snippets, and the board fixtures. When `arm-none-eabi-g++` is on the PATH it also runs the frame check (`CheckFrames.sh`), which compiles one caller of six calls for the Cortex-M4 and checks the frame sizes from `-fstack-usage`. The board fixtures (`fixtures/Board.inc`) are written from `std::regex` by `fixtures/WriteBoardFixtures.cpp`.

The snippets on this page and on the other two pages are compiled by `CheckWikiSnippets.sh`, so every example here builds.
