// expect: a range bound is outside the type of the argument
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+LED">().Write(Argument<uint8>("on"), Optional<uint8>("level").Range(0, 300));
}>;

static_assert(Set::CommandCount == 1);
