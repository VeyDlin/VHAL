// expect: a required argument comes after an optional one
#include "CommandErrorApp.h"

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("pair", "two numbers", &ErrPair, Argument<uint8>("first").Default(1), Argument<uint8>("second"));
}>;

static_assert(Set::CommandCount == 1);
