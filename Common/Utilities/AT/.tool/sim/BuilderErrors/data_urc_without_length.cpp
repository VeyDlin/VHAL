// expect: Data() needs a length argument before it
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Modules(AtDataMode)
		.Urc<"+IPD">(Data());
}>;

static_assert(sizeof(Set) > 0);
