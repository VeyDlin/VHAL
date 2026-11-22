// expect: program is over the instruction limit
// expect-value: 1276
#include <Utilities/Regex/RegexBuilder.h>

using Program = RegexCompiled<+[] {
	return RegexBuilder(R"((?:a{255}){5})");
}>;

constexpr std::size_t UsedCount = Program::Count;
