// expect: \x takes two hex digits
// expect-value: 0
#include <Utilities/Regex/RegexBuilder.h>

using Program = RegexCompiled<+[] {
	return RegexBuilder(R"(\xZ1)");
}>;

constexpr std::size_t UsedCount = Program::Count;
