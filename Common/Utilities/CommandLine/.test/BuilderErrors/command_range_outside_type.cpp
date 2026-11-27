// expect: a range bound is outside the type of the argument
#include "CommandErrorApp.h"

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("key", "press a key", &ErrKey, Argument<uint8>("code").Range(0, 300));
}>;

static_assert(Set::CommandCount == 1);
