// expect: a command name does not start with +
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"TEMP">().Run();
}>;

static_assert(Set::CommandCount == 1);
