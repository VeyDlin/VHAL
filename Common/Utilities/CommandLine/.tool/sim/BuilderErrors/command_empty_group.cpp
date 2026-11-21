// expect: an empty group has no commands or groups
#include "CommandErrorApp.h"

using EmptyGroup = CommandSet<+[] {
	return CommandSetBuilder()
		.Default(&ErrEvent);
}>;

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Group<EmptyGroup>("empty", "a group with nothing in it");
}>;

static_assert(Set::GroupCount == 1);
