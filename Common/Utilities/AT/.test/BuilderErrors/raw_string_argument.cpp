// expect: RawString is a reply value, not an argument
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+SEND">().Write(Optional<RawString<8>>("text"));
}>;

static_assert(Set::CommandCount == 1);
