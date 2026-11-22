// expect: a dialect item is given twice
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Dialect(AtDialect().Echo().Echo());
}>;

static_assert(Set::CommandCount == 0);
