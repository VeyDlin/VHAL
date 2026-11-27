// expect: a name has a character outside a-z, 0-9, _ and -
#include "CommandErrorApp.h"

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("Key", "an upper-case name", &ErrKey, Argument<uint8>("code"));
}>;

static_assert(Set::CommandCount == 1);
