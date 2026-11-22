// expect: a range is for an integer argument
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+NAME">().Write(Optional<String<8>>("name").Range(1, 3));
}>;

static_assert(Set::CommandCount == 1);
