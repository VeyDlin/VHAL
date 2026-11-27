// expect: nothing to repeat
// expect-value: 0
#include <Utilities/Regex/RegexBuilder.h>

using Program = RegexCompiled<+[] {
	return RegexBuilder(R"(*a)");
}>;

constexpr std::size_t UsedCount = Program::Count;
