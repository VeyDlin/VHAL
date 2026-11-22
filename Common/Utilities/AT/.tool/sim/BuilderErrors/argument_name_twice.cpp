// expect: two arguments of a command have the same name
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+LED">().Write(Argument<uint8>("level"), Argument<uint8>("level"));
}>;

static_assert(Set::CommandCount == 1);
