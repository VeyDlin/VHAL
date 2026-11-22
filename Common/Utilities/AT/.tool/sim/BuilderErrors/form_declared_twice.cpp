// expect: a form is declared twice for one command
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+CWMODE">().Read<int8>("mode").Read<int8>("again");
}>;

static_assert(Set::CommandCount == 1);
