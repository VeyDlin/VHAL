// expect: use \x00 for NUL
// expect-value: 0
#include <Utilities/Regex/RegexBuilder.h>

using Program = RegexCompiled<+[] {
	return RegexBuilder(R"(\0)");
}>;

constexpr std::size_t UsedCount = Program::Count;
