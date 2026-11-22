// expect: inline flags are not supported, use IgnoreCase()
// expect-value: 0
#include <Utilities/Regex/RegexBuilder.h>

using Program = RegexCompiled<+[] {
	return RegexBuilder(R"((?v)a)");
}>;

constexpr std::size_t UsedCount = Program::Count;
