// expect: the handler parameters do not match the arguments
#include "CommandErrorApp.h"

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("key", "press a key", &ErrKey, Argument<uint16>("code"));
}>;

static_assert(Set::CommandCount == 1);
