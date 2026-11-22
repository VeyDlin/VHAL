// expect: a write form has no arguments
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+LED">().Write();
}>;

static_assert(Set::CommandCount == 1);
