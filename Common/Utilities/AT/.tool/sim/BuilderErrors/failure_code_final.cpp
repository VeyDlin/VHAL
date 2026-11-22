// expect: a failure code prefix is a final line of the set
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+SEND">().Run().Finals("SENT").FailureCode("SENT");
}>;

static_assert(Set::CommandCount == 1);
