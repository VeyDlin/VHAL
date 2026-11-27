// expect: Data() in a URC needs AtDataMode among the modules of the set
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Urc<"+IPD">(Argument<uint16>("length"), Data());
}>;

static_assert(sizeof(Set) > 0);
