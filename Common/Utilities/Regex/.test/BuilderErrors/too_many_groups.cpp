// expect: more than 15 groups
// expect-value: 30
#include <Utilities/Regex/RegexBuilder.h>

using Program = RegexCompiled<+[] {
	return RegexBuilder(R"(()()()()()()()()()()()()()()()())");
}>;

constexpr std::size_t UsedCount = Program::Count;
