// expect: a command name is empty
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"">().Run();
}>;

static_assert(Set::CommandCount == 1);
