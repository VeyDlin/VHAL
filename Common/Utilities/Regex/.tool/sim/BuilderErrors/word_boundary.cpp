// expect: word boundaries are not supported
// expect-value: 0
#include <Utilities/Regex/RegexBuilder.h>

using Program = RegexCompiled<+[] {
	return RegexBuilder(R"(\bx)");
}>;

constexpr std::size_t UsedCount = Program::Count;
