// expect: a basic command has no test, read or write form
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Basic<"I">().Read<int8>("mode");
}>;

static_assert(Set::CommandCount == 1);
