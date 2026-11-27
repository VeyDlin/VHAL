// expect: pattern has too many parts
// expect-value: 2048
#include <Utilities/Regex/RegexBuilder.h>
#include <array>

namespace {


struct RepeatedA {
	std::array<char, 2049> text{};


	constexpr RepeatedA() {
		for (std::size_t index = 0; index < 2048; index++) {
			text[index] = 'a';
		}
	}
};


inline constexpr RepeatedA Repeated{};


}


using Program = RegexCompiled<+[] {
	return RegexBuilder(std::string_view(Repeated.text.data(), 2048));
}>;

constexpr std::size_t UsedCount = Program::Count;
