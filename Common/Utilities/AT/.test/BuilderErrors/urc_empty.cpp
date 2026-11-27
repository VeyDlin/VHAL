// expect: a URC prefix is empty
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Urc<"">();
}>;

static_assert(Set::UrcCount == 1);
