// expect: matching needs more stack than StackLimit allows
// expect-value: 26
#include <Utilities/Regex/RegexBuilder.h>

using Program = RegexCompiled<+[] {
	return RegexBuilder("abcdefgh").StackLimit(20);
}>;

// Matches calls this check with PlainStackBytes and StackLimit.
constexpr bool Checked = (RegexRequirePlainStack<Program::PlainStackBytes, Program::StackLimit>(), true);
