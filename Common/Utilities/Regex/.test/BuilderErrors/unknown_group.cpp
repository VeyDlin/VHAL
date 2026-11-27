// expect: unknown group syntax
// expect-value: 0
#include <Utilities/Regex/RegexBuilder.h>

using Program = RegexCompiled<+[] {
	return RegexBuilder(R"((?P<name>a))");
}>;

constexpr std::size_t UsedCount = Program::Count;
