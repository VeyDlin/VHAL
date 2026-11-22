// expect: captures need more stack than StackLimit allows
// expect-value: 108
#include <Utilities/Regex/RegexBuilder.h>

using Program = RegexCompiled<+[] {
	return RegexBuilder("(a)").StackLimit(100);
}>;

// Find calls this check with CapturesStackBytes and StackLimit.
constexpr bool Checked = (RegexRequireCapturesStack<Program::CapturesStackBytes, Program::StackLimit>(), true);
