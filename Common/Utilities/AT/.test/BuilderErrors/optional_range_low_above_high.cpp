// expect: a range has its minimum above its maximum
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+LED">().Write(Argument<uint8>("on"), Optional<uint8>("level").Range(9, 3));
}>;

static_assert(Set::CommandCount == 1);
