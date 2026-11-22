// expect: a value form has no arguments
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Basic<"E">().Value();
}>;

static_assert(Set::CommandCount == 1);
