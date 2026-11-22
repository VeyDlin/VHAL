// expect: Dialect() is given twice
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Dialect(AtDialect().Echo())
		.Dialect(AtDialect().Quiet());
}>;

static_assert(Set::CommandCount == 0);
