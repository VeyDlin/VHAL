// expect: a run form has no name
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+CIFSR">().Run<int8>("");
}>;

static_assert(Set::CommandCount == 1);
