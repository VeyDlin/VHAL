// expect: a required argument comes after an optional one
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+JOIN">().Write(Optional<uint8>("first"), Argument<uint8>("second"));
}>;

static_assert(Set::CommandCount == 1);
