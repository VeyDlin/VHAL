// expect: backreferences are not supported
// expect-value: 0
#include <Utilities/Regex/RegexBuilder.h>

using Program = RegexCompiled<+[] {
	return RegexBuilder(R"(\1)");
}>;

constexpr std::size_t UsedCount = Program::Count;
