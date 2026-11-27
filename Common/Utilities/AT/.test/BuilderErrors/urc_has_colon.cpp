// expect: a URC prefix has a colon or a comma
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Urc<"+IPD:">();
}>;

static_assert(Set::UrcCount == 1);
