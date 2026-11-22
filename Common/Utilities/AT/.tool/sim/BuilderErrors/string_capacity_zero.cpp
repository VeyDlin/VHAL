// expect: a string has a capacity of 0
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+NAME">().Write(Argument<String<0>>("name"));
}>;

static_assert(Set::CommandCount == 1);
