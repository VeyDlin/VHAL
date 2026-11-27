// expect: a command name has a character that is not A-Z or 0-9
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+temp">().Run();
}>;

static_assert(Set::CommandCount == 1);
