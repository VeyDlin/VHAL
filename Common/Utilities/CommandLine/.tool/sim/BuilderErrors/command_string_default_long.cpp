// expect: the default is longer than the string capacity
#include "CommandErrorApp.h"

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("name", "set a name", &ErrText, Argument<String<4>>("name").Default("longer than four"));
}>;

static_assert(Set::CommandCount == 1);
