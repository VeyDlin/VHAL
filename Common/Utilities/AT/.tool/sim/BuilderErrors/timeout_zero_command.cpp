// expect: a timeout of 0
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+JOIN">().Write(Argument<uint8>("mode")).TimeoutMs(0);
}>;

static_assert(Set::CommandCount == 1);
