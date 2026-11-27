// expect: escape ']' outside a class
// expect-value: 1
#include <Utilities/Regex/RegexBuilder.h>

using Program = RegexCompiled<+[] {
	return RegexBuilder(R"(a])");
}>;

constexpr std::size_t UsedCount = Program::Count;
