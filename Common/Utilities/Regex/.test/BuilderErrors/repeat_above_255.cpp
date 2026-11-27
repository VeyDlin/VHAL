// expect: repeat count above 255
// expect-value: 1
#include <Utilities/Regex/RegexBuilder.h>

using Program = RegexCompiled<+[] {
	return RegexBuilder(R"(a{256})");
}>;

constexpr std::size_t UsedCount = Program::Count;
