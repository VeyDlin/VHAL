// expect: a type that the grammar does not support
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+COUNT">().Write(Argument<uint64>("count"));
}>;

static_assert(Set::CommandCount == 1);
