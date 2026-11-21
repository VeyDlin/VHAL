// expect: two arguments of a command have the same name
#include "CommandErrorApp.h"

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("pair", "two numbers", &ErrPair, Argument<uint8>("value"), Argument<uint8>("value"));
}>;

static_assert(Set::CommandCount == 1);
