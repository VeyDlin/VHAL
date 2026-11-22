// expect: FailureCode() is given twice
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+JOIN">().Write(Argument<uint8>("code")).FailureCode("+JOIN").FailureCode("+JOIN");
}>;

static_assert(Set::CommandCount == 1);
