// expect: Modules() takes a module tag, such as AtDataMode
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Modules(42);
}>;

static_assert(Set::CommandCount == 0);
