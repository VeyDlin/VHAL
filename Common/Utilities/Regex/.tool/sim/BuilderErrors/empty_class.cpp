// expect: empty class
// expect-value: 0
#include <Utilities/Regex/RegexBuilder.h>

using Program = RegexCompiled<+[] {
	return RegexBuilder(R"([])");
}>;

constexpr std::size_t UsedCount = Program::Count;
