// expect: a failure code prefix holds a colon, a comma or a blank; the colon after it is added
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+JOIN">().Write(Argument<uint8>("code")).FailureCode("+JOIN:");
}>;

static_assert(Set::CommandCount == 1);
