// expect: an AT argument is an integer, bool or String<N>
#include <Utilities/AT/AtCommandSet.h>

enum class Mode : uint8 {
	Fast,
	Slow
};

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+MODE">().Write(Argument<Mode>("mode"));
}>;

static_assert(Set::CommandCount == 1);
