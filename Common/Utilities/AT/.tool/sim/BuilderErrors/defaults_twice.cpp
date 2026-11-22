// expect: Defaults() is given twice
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Defaults(AtDefaults().TimeoutMs(1000))
		.Defaults(AtDefaults().TimeoutMs(2000));
}>;

static_assert(Set::CommandCount == 0);
