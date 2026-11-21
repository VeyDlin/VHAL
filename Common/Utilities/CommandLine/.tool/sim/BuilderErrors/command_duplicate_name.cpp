// expect: duplicate names at one level
#include "CommandErrorApp.h"

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("key", "press a key", &ErrKey, Argument<uint8>("code"))
		.Command("key", "press it again", &ErrKey, Argument<uint8>("code"));
}>;

static_assert(Set::CommandCount == 2);
