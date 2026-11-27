// expect: a command name has a space
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+WIFI JOIN">().Run();
}>;

static_assert(Set::CommandCount == 1);
