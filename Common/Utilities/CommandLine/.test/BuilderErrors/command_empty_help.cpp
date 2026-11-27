// expect: a command has no help text
#include "CommandErrorApp.h"

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("key", "", &ErrKey, Argument<uint8>("code"));
}>;

static_assert(Set::CommandCount == 1);
