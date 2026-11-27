// expect: a basic command is not one letter A to Z
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Basic<"EE">().Run();
}>;

static_assert(Set::CommandCount == 1);
