// expect: a module is given twice
#include <Utilities/AT/AtCommandSet.h>
#include <Utilities/AT/Modules/AtDataMode.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Modules(AtDataMode)
		.Modules(AtDataMode);
}>;

static_assert(Set::CommandCount == 0);
