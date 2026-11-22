// expect: a command name is declared twice in one set
#include <Utilities/AT/AtCommandSet.h>

// ATE is the dialect's Echo, so the letter E is taken in the name tree: a basic E of the set is a second E.
using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Dialect(AtDialect().Echo())
		.Basic<"E">().Value(Argument<uint8>("on"));
}>;

static_assert(Set::CommandCount == 1);
