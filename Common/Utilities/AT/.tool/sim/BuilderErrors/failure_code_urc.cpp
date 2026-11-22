// expect: a failure code prefix is a URC prefix
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+JOIN">().Write(Argument<uint8>("code")).FailureCode("+FAIL")
		.Urc<"+FAIL">();
}>;

static_assert(Set::CommandCount == 1);
