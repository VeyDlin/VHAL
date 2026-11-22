// expect: a failure code prefix is empty
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+JOIN">().Write(Argument<uint8>("code")).FailureCode("");
}>;

static_assert(Set::CommandCount == 1);
