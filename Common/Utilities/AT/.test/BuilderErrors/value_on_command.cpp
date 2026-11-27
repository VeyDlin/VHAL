// expect: a value form is for a basic command
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+LED">().Value(Argument<uint8>("on"));
}>;

static_assert(Set::CommandCount == 1);
