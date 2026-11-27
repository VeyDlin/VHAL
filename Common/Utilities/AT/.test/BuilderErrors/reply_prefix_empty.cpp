// expect: a reply prefix is empty
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+CIFSR">().Run().Reply("");
}>;

static_assert(Set::CommandCount == 1);
