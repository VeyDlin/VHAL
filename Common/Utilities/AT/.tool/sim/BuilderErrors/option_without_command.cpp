// expect: an option is declared with no command before it
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.TimeoutMs(5000);
}>;

static_assert(Set::CommandCount == 0);
