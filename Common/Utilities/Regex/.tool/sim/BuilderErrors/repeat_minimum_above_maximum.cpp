// expect: repeat minimum above maximum
// expect-value: 1
#include <Utilities/Regex/RegexBuilder.h>

using Program = RegexCompiled<+[] {
	return RegexBuilder(R"(a{3,2})");
}>;

constexpr std::size_t UsedCount = Program::Count;
