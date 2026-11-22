// expect: a class escape cannot start or end a range
// expect-value: 1
#include <Utilities/Regex/RegexBuilder.h>

using Program = RegexCompiled<+[] {
	return RegexBuilder(R"([\d-z])");
}>;

constexpr std::size_t UsedCount = Program::Count;
