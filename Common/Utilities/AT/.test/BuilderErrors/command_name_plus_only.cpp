// expect: a command name has no letters after +
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+">().Run();
}>;

static_assert(Set::CommandCount == 1);
