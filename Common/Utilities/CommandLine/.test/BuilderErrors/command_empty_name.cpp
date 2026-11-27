// expect: a name is empty
#include "CommandErrorApp.h"

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("", "a command with no name", &ErrKey, Argument<uint8>("code"));
}>;

static_assert(Set::CommandCount == 1);
