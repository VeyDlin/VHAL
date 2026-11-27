// expect: Data() must be the last argument of a URC
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Modules(AtDataMode)
		.Urc<"+IPD">(Data(), Argument<uint16>("length"));
}>;

static_assert(sizeof(Set) > 0);
