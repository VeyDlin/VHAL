// expect: a timeout of 0
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Defaults(AtDefaults().TimeoutMs(0))
		.Command<"+GMR">().Run();
}>;

static_assert(Set::CommandCount == 1);
