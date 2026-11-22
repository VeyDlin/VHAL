// expect: a form is declared twice for one command
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+CWMODE">().Run().Run<int8>("mode");
}>;

static_assert(Set::CommandCount == 1);
