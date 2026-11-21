// expect: the default is outside the range of the argument
#include "CommandErrorApp.h"

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("key", "press a key", &ErrKey, Argument<uint8>("code").Range(10, 20).Default(5));
}>;

static_assert(Set::CommandCount == 1);
