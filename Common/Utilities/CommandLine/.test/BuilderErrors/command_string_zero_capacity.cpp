// expect: a string argument needs a capacity of at least 1
#include "CommandErrorApp.h"

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("name", "set a name", &ErrKey, Argument<String<0>>("name"));
}>;

static_assert(Set::CommandCount == 1);
