// expect: a form is declared with no command before it
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Read<int8>("mode");
}>;

static_assert(Set::CommandCount == 0);
