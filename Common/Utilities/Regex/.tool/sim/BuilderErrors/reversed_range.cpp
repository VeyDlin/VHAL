// expect: reversed range
// expect-value: 1
#include <Utilities/Regex/RegexBuilder.h>

using Program = RegexCompiled<+[] {
	return RegexBuilder(R"([z-a])");
}>;

constexpr std::size_t UsedCount = Program::Count;
